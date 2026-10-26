// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <map>
#include <juce_data_structures/juce_data_structures.h>

namespace altvane::ui {
    /**
     * per-user editor preferences, stored in settings.xml next to the presets
     * products add their own on / off preferences as named switches
     */
    struct UISettings {
        static constexpr std::array kScales{.8f, .9f, 1.f, 1.1f, 1.25f, 1.5f};

        float scale{1.f};
        int accent{0};
        bool reduce_motion{false};
        float drag_sensitivity{1.f};
        float wheel_sensitivity{1.f};
        // product preferences: key -> value; declare them with their defaults before load()
        std::map<juce::String, bool> switches;

        bool getSwitch(const juce::String& key) const {
            const auto it = switches.find(key);
            return it != switches.end() && it->second;
        }

        void load(const juce::PropertiesFile& file);

        void save(juce::PropertiesFile& file) const;

        /**
         * push the preferences into the global theme / interaction state
         */
        void apply() const;

        /**
         * settings.xml in the user's application data folder, e.g. Application Support/Altvane/Altvane EQ
         */
        static std::unique_ptr<juce::PropertiesFile> openFile();
    };
}
