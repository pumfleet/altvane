// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "common.hpp"

namespace altvane::leveler::dsp {
    /**
     * the gain computer: rides toward the target while the voice sings, holds through the gaps between syllables
     * and drifts to the idle gain only after a longer silence; all in dB, once per hop
     */
    class Rider {
    public:
        struct Settings {
            float target_db{-20.f};
            // 0 = slowest (1.5 s), 1 = fastest (40 ms)
            float speed{.5f};
            // 0 = unvoiced sounds ride like the voice, 1 = they never move the gain
            float guard{.7f};
            float range_min{-6.f}, range_max{6.f};
            float idle_db{0.f};
        };

        // gaps shorter than this are part of the phrase: the gain holds
        static constexpr double kHoldSeconds = .3;
        // after a longer silence, the first voiced hops of a phrase catch up at once: with lookahead, the move
        // happens while the output is still silent
        static constexpr double kOnsetSeconds = .03;
        static constexpr double kOnsetTime = .005;

        void prepare(double hop_seconds);

        void reset(float gain_db);

        /**
         * one hop: the voiced level (voiced hops only) and the level of everything above the floor
         * @return the gain, in dB
         */
        float update(HopState state, float voiced_db, float level_db, const Settings& settings);

        /**
         * take over the gain (manual ride, automation read); riding continues from here
         */
        void setGain(const float gain_db) { gain_ = gain_db; }

        float getGain() const { return gain_; }

        /**
         * boost time constant for a speed, in seconds; cuts are twice as fast
         */
        static double boostTime(float speed);

    private:
        double hop_{.005};
        float gain_{0.f};
        double silent_time_{1e9}, onset_left_{0.0};

        void moveToward(float desired, double time, float scale = 1.f);
    };
}
