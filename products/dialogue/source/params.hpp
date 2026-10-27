// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/stems.hpp"

/**
 * Altvane Dialogue's parameters; the IDs are part of every saved session and preset, never change them
 */
namespace altvane::dialogue::params {
    inline constexpr auto kVoice = "voice_level";
    inline constexpr auto kAmbience = "ambience_level";
    inline constexpr auto kReverb = "reverb_level";
    inline constexpr auto kQuality = "quality";
    inline constexpr auto kStereo = "stereo_mode";
    inline constexpr auto kOutput = "output_gain";
    inline constexpr auto kBypass = "bypass";

    // stem levels: the bottom of the range is Off
    inline constexpr float kLevelOff = -60.f;
    inline constexpr float kLevelMax = 12.f;

    inline constexpr std::array<const char*, dsp::kNumStems> kLevelIDs{kVoice, kAmbience, kReverb};
    inline constexpr std::array<const char*, dsp::kNumStems> kStemNames{"Voice", "Ambience", "Reverb"};
    inline constexpr std::array<float, dsp::kNumStems> kLevelDefaults{0.f, -24.f, -12.f};
    inline const juce::StringArray kQualityChoices{"Eco", "High"};
    // in dsp::StereoMode order; new modes go at the end
    inline const juce::StringArray kStereoChoices{"Linked", "Stereo", "Mid/Side"};

    /**
     * e.g. "voice_sens_3", node 0 .. kSensitivityNodes - 1
     */
    inline juce::String sensitivityID(const size_t stem, const size_t node) {
        static constexpr std::array<const char*, dsp::kNumStems> kPrefixes{"voice_sens_", "ambience_sens_", "reverb_sens_"};
        return kPrefixes[stem] + juce::String(static_cast<int>(node) + 1);
    }

    inline float levelToGain(const float db) {
        return db <= kLevelOff + .05f ? 0.f : juce::Decibels::decibelsToGain(db);
    }

    inline juce::String formatLevel(const float db) {
        if (db <= kLevelOff + .05f) {
            return "Off";
        }
        // the typographic minus, as everywhere else in the interface
        return (db > .04f ? "+" : db < -.04f ? juce::String::fromUTF8("\xe2\x88\x92") : "") + juce::String(std::abs(db), 1) + " dB";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /**
     * every automatable parameter ID, in the order the layout declares them (the frozen list the tests check)
     */
    juce::StringArray allIDs();
}
