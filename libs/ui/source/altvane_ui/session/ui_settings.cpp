// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "ui_settings.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

#include "../theme/tokens.hpp"
#include "../util/spring.hpp"

namespace altvane::ui {
    void UISettings::load(const juce::PropertiesFile& file) {
        scale = std::clamp(static_cast<float>(file.getDoubleValue("interface_scale", 1.0)), kScales.front(), kScales.back());
        accent = std::clamp(file.getIntValue("accent", 0), 0, static_cast<int>(tokens::colour::kAccentChoices.size()) - 1);
        reduce_motion = file.getBoolValue("reduce_motion", false);
        drag_sensitivity = std::clamp(static_cast<float>(file.getDoubleValue("drag_sensitivity", 1.0)), .25f, 4.f);
        wheel_sensitivity = std::clamp(static_cast<float>(file.getDoubleValue("wheel_sensitivity", 1.0)), .25f, 4.f);
        for (auto& [key, value] : switches) {
            value = file.getBoolValue(key, value);
        }
    }

    void UISettings::save(juce::PropertiesFile& file) const {
        file.setValue("interface_scale", static_cast<double>(scale));
        file.setValue("accent", accent);
        file.setValue("reduce_motion", reduce_motion);
        file.setValue("drag_sensitivity", static_cast<double>(drag_sensitivity));
        file.setValue("wheel_sensitivity", static_cast<double>(wheel_sensitivity));
        for (const auto& [key, value] : switches) {
            file.setValue(key, value);
        }
    }

    void UISettings::apply() const {
        tokens::colour::kAccent = tokens::colour::kAccentChoices[static_cast<size_t>(accent)].second;
        InteractionSettings::reduce_motion = reduce_motion;
        InteractionSettings::drag_sensitivity = drag_sensitivity;
        InteractionSettings::wheel_sensitivity = wheel_sensitivity;
    }

    std::unique_ptr<juce::PropertiesFile> UISettings::openFile() {
        juce::PropertiesFile::Options options;
        options.applicationName = "settings";
        options.filenameSuffix = ".xml";
        options.folderName = juce::String(JucePlugin_Manufacturer) + juce::File::getSeparatorString()
            + JucePlugin_Name;
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 500;
        return std::make_unique<juce::PropertiesFile>(options);
    }
}
