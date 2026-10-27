// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_core/juce_core.h>

namespace altvane::deesser {
    // bump when the set changes, so new presets are installed for existing users (edited ones are never overwritten)
    inline constexpr int kFactoryPresetsVersion = 1;

    /**
     * relative path ("Factory/Female Vocal") -> preset file contents
     */
    std::vector<std::pair<juce::String, juce::String>> factoryPresets();
}
