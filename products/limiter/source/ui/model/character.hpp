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
#include <cmath>
#include <optional>

#include "../../zlp/zlp_definitions.hpp"

namespace altvane::limiter {
    /**
     * a starting point for the timing controls, picked in one go: these set ZL Limiter's lookahead, attack,
     * release, recovery and channel delta (they are presets for the one limiter, not separate algorithms)
     *
     * recovery is how fast the peak stage lets go; attack and release drive the slower stage that carries
     * sustained reduction; channel delta is how far the two channels may differ
     *
     * the list runs from gentle to loud: at the same gain on a drum loop, each one is louder than the one before
     * it, Safe being the quietest (tests/processor_tests.cpp checks the order)
     */
    struct Character {
        const char* name;
        const char* description;
        float lookahead, attack, release, recovery, channel_delta;
    };

    inline constexpr std::array<Character, 5> kCharacters{
        {
            {"Transparent", "Long lookahead, slow and smooth recovery: the least colour, for finished mixes.",
             5.f, 200.f, 1000.f, 100.f, 1.f},
            {"Balanced", "The engine's defaults: clean peaks and a natural recovery for most material.",
             zlp::PLookahead::kDefaultV, zlp::PAttack::kDefaultV, zlp::PRelease::kDefaultV,
             zlp::PRecovery::kDefaultV, zlp::PChannelDelta::kDefaultV},
            {"Punchy", "Short lookahead and quick recovery between hits keep drums forward, and it is louder.",
             1.f, 300.f, 300.f, 20.f, 3.f},
            {"Aggressive", "The fastest release and recovery: the most loudness, with a density you can hear.",
             .5f, 100.f, 40.f, 3.5f, 4.f},
            {"Safe", "Fully linked and slow to let go: catches overs without moving the mix.",
             5.f, 100.f, 1500.f, 200.f, 0.f},
        }
    };

    inline constexpr std::array kCharacterParameterIDs{
        zlp::PLookahead::kID, zlp::PAttack::kID, zlp::PRelease::kID, zlp::PRecovery::kID, zlp::PChannelDelta::kID
    };

    inline std::array<float, 5> characterValues(const Character& c) {
        return {c.lookahead, c.attack, c.release, c.recovery, c.channel_delta};
    }

    /**
     * the character the timing values match, if any
     */
    inline std::optional<size_t> findCharacter(const std::array<float, 5>& values) {
        for (size_t i = 0; i < kCharacters.size(); ++i) {
            const auto preset = characterValues(kCharacters[i]);
            bool same = true;
            for (size_t k = 0; k < values.size(); ++k) {
                same = same && std::abs(values[k] - preset[k]) <= .01f + .005f * std::abs(preset[k]);
            }
            if (same) {
                return i;
            }
        }
        return std::nullopt;
    }
}
