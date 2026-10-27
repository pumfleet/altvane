// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Altvane Leveler's parameters; the IDs are part of every saved session and preset, never change them
 */
namespace altvane::leveler::params {
    inline constexpr auto kTarget = "target";
    inline constexpr auto kSensitivity = "sensitivity";
    inline constexpr auto kSpeed = "speed";
    inline constexpr auto kGuard = "guard";
    inline constexpr auto kMusic = "music";
    inline constexpr auto kRangeMin = "range_min";
    inline constexpr auto kRangeMax = "range_max";
    inline constexpr auto kIdle = "idle";
    inline constexpr auto kLookahead = "lookahead";
    inline constexpr auto kOutput = "output";
    inline constexpr auto kBypass = "bypass";

    /**
     * the rider fader: automatable, but not part of the parameter tree, so the engine's moves never reach the
     * undo history and presets and A/B never recall it
     */
    inline constexpr auto kRide = "ride";

    inline constexpr float kTargetMin = -42.f, kTargetMax = 0.f, kTargetDefault = -20.f;
    inline constexpr float kRangeLimit = 24.f;
    inline constexpr float kRideLimit = 24.f;

    /**
     * signed dB with the typographic minus, as everywhere else in the interface
     */
    inline juce::String formatDb(const float db, const int decimals = 1) {
        return (db > .04f ? "+" : db < -.04f ? juce::String::fromUTF8("\xe2\x88\x92") : "")
               + juce::String(std::abs(db), decimals) + " dB";
    }

    inline juce::String formatPercent(const float v) {
        return juce::String(juce::roundToInt(v * 100.f)) + "%";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::unique_ptr<juce::AudioParameterFloat> createRide();

    /**
     * every parameter ID in the tree, in the order the layout declares them (the frozen list the tests check)
     */
    juce::StringArray allIDs();
}
