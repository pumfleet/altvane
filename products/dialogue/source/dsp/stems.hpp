// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "spectral.hpp"

namespace altvane::dialogue::dsp {
    enum Stem { kVoice, kAmbience, kReverb, kNumStems };

    /**
     * what the output plays: the mix, one stem on its own, or what the mix removes from the input
     */
    enum class Listen { kMix, kVoice, kAmbience, kReverb, kRemoved };

    inline constexpr int kSensitivityNodes = 6;
    inline constexpr std::array<float, kSensitivityNodes> kSensitivityFrequencies{120.f, 300.f, 750.f, 1800.f, 4500.f, 11000.f};

    using BinWeights = std::array<float, kBins>;

    /**
     * every stem as a complex, per-bin share of the input frame; the shares always sum to one,
     * so the stems always sum to the input
     */
    struct StemShares {
        std::array<Spectrum, kNumStems> share;
    };

    /**
     * splits one analysis stream (a channel, or the mid of linked channels) into dry voice, ambience and voice reverb:
     * - the model's speech estimate S is the voice, X - S the ambience
     * - the late reverb of the voice is predicted from the voice's energy 50 ms earlier (an exponential decay model)
     *   and taken from S and X - S in proportion, since the model leaves part of the tail in each
     */
    class StemSplitter {
    public:
        StemSplitter();

        void reset();

        /**
         * @param x the input frame the estimate belongs to
         * @param speech the model's speech estimate of that frame
         */
        void split(std::span<const Complex, kBins> x, std::span<const Complex, kBins> speech, StemShares& out);

    private:
        static constexpr int kEarlyFrames = 5; // 50 ms: reflections before this belong to the dry voice
        BinWeights smooth_speech_{}, smooth_rest_{}, floor_{};
        std::array<BinWeights, kEarlyFrames + 1> voice_history_{};
        int history_pos_{0};
        float late_gain_;
    };

    /**
     * per-bin weights (x0.25 .. x4) from a stem's sensitivity nodes (-1 .. 1), smooth on a log-frequency axis
     */
    void computeSensitivity(const std::array<float, kSensitivityNodes>& nodes, BinWeights& weights);

    /**
     * the per-bin transfer applied to the input frame, and the stems after sensitivity for the meters
     * @param weights per-stem sensitivity weights; all ones leave the shares unchanged
     * @param gains per-stem linear gains
     */
    void computeTransfer(const StemShares& shares, const std::array<BinWeights, kNumStems>& weights,
                         const std::array<float, kNumStems>& gains, Listen listen,
                         Spectrum& transfer, StemShares& weighted);
}
