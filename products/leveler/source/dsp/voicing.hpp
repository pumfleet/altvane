// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <vector>

#include "filters.hpp"

namespace altvane::leveler::dsp {
    /**
     * is this hop a voice? a pitch-confidence detector (YIN's cumulative mean normalised difference) on the
     * signal decimated to about 8 kHz, plus how much of the energy is above 4 kHz (sibilants)
     * breaths, sibilants, noise and drum spill are aperiodic, so their confidence stays low whatever their level
     */
    class VoicingDetector {
    public:
        struct Result {
            // 0 (noise) .. 1 (perfectly periodic)
            float periodicity{0.f};
            // share of the energy above 4 kHz over the last 20 ms
            float high_ratio{0.f};
        };

        void prepare(double sample_rate, int hop_samples);

        void reset();

        /**
         * one sample of the (channel-summed) input
         */
        void push(float x);

        /**
         * at the end of each hop
         */
        Result analyse();

    private:
        static constexpr size_t kRingSize = 1024;
        static constexpr size_t kEnergyHops = 4;

        Biquad anti_alias_a_, anti_alias_b_, high_;
        bool has_high_band_{true};
        int decimation_{6}, phase_{0};
        size_t write_{0}, window_{240}, min_lag_{10}, max_lag_{133};
        std::array<float, kRingSize> ring_{};
        std::vector<float> frame_, diff_;
        std::array<double, kEnergyHops> high_energy_{}, total_energy_{};
        size_t energy_slot_{0};
        double hop_high_{0.0}, hop_total_{0.0};
    };
}
