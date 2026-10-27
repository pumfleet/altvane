// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <altvane_ui/session/preset_manager.hpp>

class PluginProcessor;

namespace altvane::limiter {
    // bump when the set changes, so new presets are installed for existing users (edited ones are never overwritten)
    inline constexpr int kFactoryPresetsVersion = 1;

    /**
     * relative path ("Factory/Streaming Master") -> preset file contents (ZL Limiter's preset format)
     * every value not named by a preset is the parameter's default
     */
    std::vector<std::pair<juce::String, juce::String>> factoryPresets(PluginProcessor& p);

    /**
     * presets are ZL Limiter preset files; loading one keeps the display and loudness preferences of the session
     * and takes only its loudness target from the preset
     */
    altvane::ui::PresetCodec presetCodec(PluginProcessor& p);
}
