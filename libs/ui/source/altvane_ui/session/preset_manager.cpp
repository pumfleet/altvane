// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "preset_manager.hpp"

#include <algorithm>

namespace altvane::ui {
    namespace {
        juce::var xmlToJson(const juce::XmlElement& xml) {
            auto* obj = new juce::DynamicObject();
            obj->setProperty("tag", xml.getTagName());
            for (int i = 0; i < xml.getNumAttributes(); ++i) {
                obj->setProperty(xml.getAttributeName(i), xml.getAttributeValue(i));
            }
            juce::Array<juce::var> children;
            for (const auto* child : xml.getChildIterator()) {
                children.add(xmlToJson(*child));
            }
            if (!children.isEmpty()) {
                obj->setProperty("children", children);
            }
            return obj;
        }

        std::unique_ptr<juce::XmlElement> jsonToXml(const juce::var& json) {
            const auto* obj = json.getDynamicObject();
            if (obj == nullptr || !obj->hasProperty("tag")) {
                return nullptr;
            }
            auto xml = std::make_unique<juce::XmlElement>(obj->getProperty("tag").toString());
            for (const auto& prop : obj->getProperties()) {
                if (prop.name.toString() != "tag" && prop.name.toString() != "children") {
                    xml->setAttribute(prop.name, prop.value.toString());
                }
            }
            if (const auto* children = obj->getProperty("children").getArray()) {
                for (const auto& c : *children) {
                    if (auto child = jsonToXml(c)) {
                        xml->addChildElement(child.release());
                    }
                }
            }
            return xml;
        }
    }

    PresetCodec PresetCodec::xmlAsJson() {
        PresetCodec codec;
        codec.read = [](const juce::File& file, juce::MemoryBlock& state) {
            const auto json = juce::JSON::parse(file);
            const auto xml = jsonToXml(json);
            if (xml == nullptr) {
                return juce::Result::fail("Not a preset file");
            }
            state.reset();
            juce::AudioProcessor::copyXmlToBinary(*xml, state);
            return juce::Result::ok();
        };
        codec.write = [](const juce::File& file, const juce::MemoryBlock& state) {
            const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
            if (xml == nullptr) {
                return juce::Result::fail("Invalid state");
            }
            return file.replaceWithText(juce::JSON::toString(xmlToJson(*xml)))
                       ? juce::Result::ok()
                       : juce::Result::fail("Could not write " + file.getFullPathName());
        };
        return codec;
    }

    PresetManager::PresetManager(juce::AudioProcessor& processor, ParamEditor& editor, Session& session,
                                 PresetCodec codec) :
        processor_(processor), editor_(editor), session_(session), codec_(std::move(codec)) {
        captureReference();
    }

    juce::File PresetManager::getDirectory() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile(JucePlugin_Manufacturer).getChildFile(JucePlugin_Name).getChildFile("Presets");
    }

    void PresetManager::installFactoryPresets(const std::vector<std::pair<juce::String, juce::String>>& presets,
                                              const int version) {
        if (settings_ != nullptr && settings_->getIntValue("factory_presets", 0) >= version) {
            return;
        }
        const auto dir = getDirectory();
        for (const auto& [path, contents] : presets) {
            const auto file = dir.getChildFile(path + kExtension);
            if (!file.existsAsFile()) {
                file.getParentDirectory().createDirectory();
                file.replaceWithText(contents);
            }
        }
        if (settings_ != nullptr) {
            settings_->setValue("factory_presets", version);
        }
    }

    std::vector<PresetManager::Entry> PresetManager::scan() const {
        const auto dir = getDirectory();
        std::vector<Entry> result;
        for (const auto& file : dir.findChildFiles(juce::File::findFiles, true, juce::String("*") + kExtension)) {
            const auto parent = file.getParentDirectory();
            result.push_back({
                file, file.getFileNameWithoutExtension(),
                parent == dir ? juce::String() : parent.getRelativePathFrom(dir), isFavourite(file)
            });
        }
        std::ranges::sort(result, [](const Entry& a, const Entry& b) {
            return a.name.compareNatural(b.name) < 0;
        });
        return result;
    }

    juce::StringArray PresetManager::getGroups() const {
        juce::StringArray groups;
        const auto dir = getDirectory();
        for (const auto& sub : dir.findChildFiles(juce::File::findDirectories, true)) {
            groups.add(sub.getRelativePathFrom(dir));
        }
        groups.sortNatural();
        return groups;
    }

    bool PresetManager::load(const juce::File& file) {
        juce::MemoryBlock state;
        if (!codec_.read(file, state).wasOk()) {
            return false;
        }
        if (editor_.isEditing()) {
            editor_.endEdit();
        }
        processor_.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        // undo cannot meaningfully cross a preset load
        session_.undo_manager.clearUndoHistory();
        session_.current_preset = file;
        captureReference();
        return true;
    }

    bool PresetManager::save(const juce::File& file) {
        file.getParentDirectory().createDirectory();
        juce::MemoryBlock state;
        processor_.getStateInformation(state);
        if (!codec_.write(file, state).wasOk()) {
            return false;
        }
        session_.current_preset = file;
        captureReference();
        return true;
    }

    void PresetManager::initialise() {
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        for (auto* p : processor_.getParameters()) {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p)) {
                values.emplace_back(ranged, ranged->convertFrom0to1(ranged->getDefaultValue()));
            }
        }
        editor_.setNow(values, "Initialise");
        session_.current_preset = juce::File();
        captureReference();
    }

    void PresetManager::step(const int delta) {
        const auto presets = scan();
        if (presets.empty()) {
            return;
        }
        const auto current = getCurrentFile();
        const auto it = std::ranges::find_if(presets, [&](const Entry& e) { return e.file == current; });
        auto idx = it == presets.end() ? (delta > 0 ? -1 : 0) : static_cast<int>(std::distance(presets.begin(), it));
        const auto n = static_cast<int>(presets.size());
        idx = ((idx + delta) % n + n) % n;
        load(presets[static_cast<size_t>(idx)].file);
    }

    bool PresetManager::rename(const juce::File& file, const juce::String& new_name) {
        const auto name = juce::File::createLegalFileName(new_name.trim());
        if (name.isEmpty()) {
            return false;
        }
        const auto target = file.getSiblingFile(name + kExtension);
        if (target.exists() || !file.moveFileTo(target)) {
            return false;
        }
        if (isFavourite(file)) {
            setFavourite(file, false);
            setFavourite(target, true);
        }
        if (getCurrentFile() == file) {
            session_.current_preset = target;
        }
        return true;
    }

    bool PresetManager::remove(const juce::File& file) {
        setFavourite(file, false);
        if (getCurrentFile() == file) {
            session_.current_preset = juce::File();
        }
        return file.moveToTrash();
    }

    juce::File PresetManager::createGroup(const juce::String& name) const {
        const auto dir = getDirectory().getChildFile(juce::File::createLegalFileName(name.trim()));
        dir.createDirectory();
        return dir;
    }

    juce::StringArray PresetManager::getFavourites() const {
        if (settings_ == nullptr) {
            return {};
        }
        return juce::StringArray::fromLines(settings_->getValue("favourite_presets"));
    }

    bool PresetManager::isFavourite(const juce::File& file) const {
        return getFavourites().contains(file.getRelativePathFrom(getDirectory()));
    }

    void PresetManager::setFavourite(const juce::File& file, const bool favourite) {
        if (settings_ == nullptr) {
            return;
        }
        auto favourites = getFavourites();
        const auto key = file.getRelativePathFrom(getDirectory());
        favourites.removeString(key);
        if (favourite) {
            favourites.add(key);
        }
        favourites.removeEmptyStrings();
        settings_->setValue("favourite_presets", favourites.joinIntoString("\n"));
    }

    juce::String PresetManager::getCurrentName() const {
        const auto file = getCurrentFile();
        return file == juce::File() ? juce::String("Default") : file.getFileNameWithoutExtension();
    }

    void PresetManager::captureReference() {
        const auto& params = processor_.getParameters();
        reference_.resize(static_cast<size_t>(params.size()));
        for (int i = 0; i < params.size(); ++i) {
            reference_[static_cast<size_t>(i)] = params[i]->getValue();
        }
    }

    bool PresetManager::isModified() const {
        const auto& params = processor_.getParameters();
        if (reference_.size() != static_cast<size_t>(params.size())) {
            return true;
        }
        for (int i = 0; i < params.size(); ++i) {
            if (std::abs(params[i]->getValue() - reference_[static_cast<size_t>(i)]) > 1e-5f) {
                return true;
            }
        }
        return false;
    }
}
