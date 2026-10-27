// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_core/juce_core.h>

namespace altvane::leveler {
    // bump when the set changes, so new presets are installed for existing users (edited ones are never overwritten)
    inline constexpr int kFactoryPresetsVersion = 1;

    /**
     * relative path ("Factory/Lead Vocal") -> preset file contents
     */
    std::vector<std::pair<juce::String, juce::String>> factoryPresets();
}
