// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "preset_manager.hpp"

#include <algorithm>

#include "../../state/preset_json.hpp"

namespace contour::ui {
    PresetManager::PresetManager(EngineBridge& bridge) : bridge_(bridge) {
        captureReference();
    }

    juce::File PresetManager::getDirectory() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile(JucePlugin_Manufacturer).getChildFile(JucePlugin_Name).getChildFile("Presets");
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
        if (!zlstate::PresetJson::read(file, state).wasOk()) {
            return false;
        }
        if (bridge_.isEditing()) {
            bridge_.endEdit();
        }
        bridge_.getProcessor().setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        // undo cannot meaningfully cross a preset load
        bridge_.getUndoManager().clearUndoHistory();
        bridge_.getProcessor().current_preset_ = file;
        captureReference();
        return true;
    }

    bool PresetManager::save(const juce::File& file) {
        file.getParentDirectory().createDirectory();
        juce::MemoryBlock state;
        bridge_.getProcessor().getStateInformation(state);
        if (!zlstate::PresetJson::write(file, state).wasOk()) {
            return false;
        }
        bridge_.getProcessor().current_preset_ = file;
        captureReference();
        return true;
    }

    void PresetManager::initialise() {
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        for (auto* p : bridge_.getProcessor().getParameters()) {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p)) {
                values.emplace_back(ranged, ranged->convertFrom0to1(ranged->getDefaultValue()));
            }
        }
        bridge_.setNow(values, "Initialise");
        bridge_.getProcessor().current_preset_ = juce::File();
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
            bridge_.getProcessor().current_preset_ = target;
        }
        return true;
    }

    bool PresetManager::remove(const juce::File& file) {
        setFavourite(file, false);
        if (getCurrentFile() == file) {
            bridge_.getProcessor().current_preset_ = juce::File();
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
        const auto& params = bridge_.getProcessor().getParameters();
        reference_.resize(static_cast<size_t>(params.size()));
        for (int i = 0; i < params.size(); ++i) {
            reference_[static_cast<size_t>(i)] = params[i]->getValue();
        }
    }

    bool PresetManager::isModified() const {
        const auto& params = bridge_.getProcessor().getParameters();
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
