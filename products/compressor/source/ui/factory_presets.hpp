// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "altvane_ui/session/preset_manager.hpp"

class PluginProcessor;

namespace altvane::compressor {
    // bump when the set changes, so new presets are installed for existing users (edited ones are never overwritten)
    inline constexpr int kFactoryPresetsVersion = 1;

    /**
     * relative path ("Factory/Drum Smash") -> preset file contents (ZL Compressor's preset format)
     * every value not named by a preset is the parameter's default
     */
    std::vector<std::pair<juce::String, juce::String>> factoryPresets(PluginProcessor& p);

    /**
     * presets are ZL Compressor preset files; loading one keeps the session's display (time, scale, open panels),
     * which ZL Compressor saves in its presets too
     */
    ui::PresetCodec presetCodec(PluginProcessor& p);
}
