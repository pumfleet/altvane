// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Altvane De-Esser's parameters; the IDs are part of every saved session and preset, never change them
 */
namespace altvane::deesser::params {
    inline constexpr auto kThreshold = "threshold";
    inline constexpr auto kRange = "range";
    inline constexpr auto kMode = "mode";
    inline constexpr auto kProcessing = "processing";
    inline constexpr auto kShape = "shape";
    inline constexpr auto kBandLow = "band_low";
    inline constexpr auto kBandHigh = "band_high";
    inline constexpr auto kLookahead = "lookahead";
    inline constexpr auto kStereoLink = "stereo_link";
    inline constexpr auto kStereoMode = "stereo_mode";
    inline constexpr auto kSideChain = "side_chain";
    inline constexpr auto kOutput = "output_gain";
    inline constexpr auto kBypass = "bypass";

    inline const juce::StringArray kModeChoices{"Vocal", "Allround"};
    inline const juce::StringArray kProcessingChoices{"Wide", "Split"};
    inline const juce::StringArray kShapeChoices{"Shelf", "Bell"};
    inline const juce::StringArray kStereoChoices{"Stereo", "Mid", "Side"};
    inline const juce::StringArray kSideChainChoices{"Internal", "External"};

    inline constexpr float kThresholdMin = -60.f, kThresholdMax = 0.f, kThresholdDefault = -24.f;
    inline constexpr float kRangeMax = 24.f, kRangeDefault = 9.f;
    inline constexpr float kBandLowMin = 1000.f, kBandLowMax = 16000.f, kBandLowDefault = 5500.f;
    inline constexpr float kBandHighMin = 2000.f, kBandHighMax = 20000.f;
    // the band's upper edge at the top of its range is open: no low-pass on the detection
    inline constexpr float kBandOpen = 19900.f;
    // the edges never come closer than this ratio (a third of an octave)
    inline constexpr float kMinBandRatio = 1.26f;
    inline constexpr float kLookaheadMax = 15.f, kLookaheadDefault = 5.f;

    inline bool isOpen(const float band_high) {
        return band_high >= kBandOpen;
    }

    /**
     * a valid band from wanted edges: each within its range, the high edge (unless open) at least kMinBandRatio
     * above the low one; when they collide the edge being set wins and the other gives way
     * (the engine keeps the same rule with the low edge winning)
     */
    std::pair<float, float> constrainBand(float low, float high, bool low_wins);

    juce::String formatDb(float db);

    juce::String formatHz(float hz);

    juce::String formatBandHigh(float hz);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /**
     * every automatable parameter ID, in the order the layout declares them (the frozen list the tests check)
     */
    juce::StringArray allIDs();
}
