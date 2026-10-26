// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "../common.hpp"

namespace altvane::eq {
    /**
     * per-user editor preferences, stored in settings.xml next to the presets
     */
    struct UISettings {
        static constexpr std::array kScales{.8f, .9f, 1.f, 1.1f, 1.25f, 1.5f};

        float scale{1.f};
        int accent{0};
        bool reduce_motion{false};
        bool spectrum_grab{true};
        float drag_sensitivity{1.f};
        float wheel_sensitivity{1.f};

        void load(const juce::PropertiesFile& file);

        void save(juce::PropertiesFile& file) const;

        /**
         * push the preferences into the global theme / interaction state
         */
        void apply() const;
    };
}
