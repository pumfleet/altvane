// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace altvane::leveler::dsp {
    // every detector and the rider run once per hop, counted in samples, so nothing depends on the block size
    inline constexpr double kHopSeconds = .005;
    // lookahead, in hops (30 ms): enough for the voicing detector to hear a phrase start before it is played
    inline constexpr int kLookaheadHops = 6;
    // the floor of every level reading, in dB
    inline constexpr float kSilenceDb = -100.f;

    /**
     * what the detector heard in one hop
     */
    enum class HopState : uint8_t {
        kSilent, // below the sensitivity floor
        kUnvoiced, // above the floor but not a voice: breaths, sibilants, noise, atonal spill
        kVoiced
    };

    /**
     * automation mode, as Vocal Rider has it
     */
    enum class Mode : int {
        kOff, // ride, tell the host nothing
        kWrite, // ride, and write the rider fader to the host's automation
        kRead // follow the rider fader's automation instead of riding
    };

    /**
     * one hop of history for the editor, lined up on output time:
     * the input level is the audio the gain was applied to
     */
    struct Column {
        float in_db{kSilenceDb}, out_db{kSilenceDb}, key_db{kSilenceDb};
        float gain_db{0.f}, target_db{0.f}, floor_db{0.f};
        HopState state{HopState::kSilent};
    };

    /**
     * BS.1770 loudness of a mean square (K-weighted, summed over channels)
     */
    inline float loudnessDb(const double mean_square) {
        return mean_square > 1e-10 ? static_cast<float>(-.691 + 10.0 * std::log10(mean_square)) : kSilenceDb;
    }
}
