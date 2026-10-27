// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <array>
#include <optional>

namespace altvane::limiter {
    /**
     * a delivery target: integrated loudness and the true-peak limit
     */
    struct LoudnessTarget {
        const char* name;
        // the line shown with the target, e.g. "Spotify, Tidal, Amazon"
        const char* detail;
        std::optional<float> lufs;
        float true_peak;
    };

    /**
     * in the order of zlstate::PLoudnessTarget's choices; the last one is replaced by the custom values
     */
    inline constexpr std::array<LoudnessTarget, 8> kLoudnessTargets{
        {
            {"Streaming", "Spotify, Tidal, Amazon Music", -14.f, -1.f},
            {"Apple Music", "Apple Music, Sound Check", -16.f, -1.f},
            {"YouTube", "YouTube", -14.f, -1.f},
            {"EBU R128", "European broadcast", -23.f, -1.f},
            {"ATSC A/85", "US broadcast", -24.f, -2.f},
            {"Podcast", "Spoken word, Apple Podcasts", -16.f, -1.f},
            {"CD / Club", "No loudness target, peaks only", std::nullopt, -.1f},
            {"Custom", "Your own target", -14.f, -1.f},
        }
    };

    inline constexpr size_t kCustomTarget = kLoudnessTargets.size() - 1;
}
