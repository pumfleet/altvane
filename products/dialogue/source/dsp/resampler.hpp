// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <cstdint>
#include <vector>

namespace altvane::dialogue::dsp {
    /**
     * streaming rational sample-rate conversion (polyphase, Kaiser-windowed sinc), one sample in at a time:
     * no block bursts, a fixed delay, and no allocation after prepare()
     *
     * both directions of a round trip share the common rate R = lcm(a, b); with the same prototype length K on both
     * sides the round trip delays by (K - 1) samples at R, which prepare() makes a whole number of host samples
     */
    class Resampler {
    public:
        /**
         * @param prototype_length K, the filter length at the common rate
         * @param cutoff passband edge in Hz (everything above is the round trip's loss)
         */
        void prepare(int from, int to, int prototype_length, double cutoff);

        void reset();

        /**
         * @return the number of samples written to out (at most maxOutput(n))
         */
        int process(const float* in, int n, float* out);

        int maxOutput(int n) const;

        /**
         * the common rate R = lcm(from, to)
         */
        static int64_t commonRate(int a, int b);

    private:
        int64_t up_{1}, down_{1}; // L = R / from, M = R / to
        int taps_{0};             // per phase
        std::vector<float> coefficients_; // phase-major, L * taps
        std::vector<float> history_;      // last taps inputs, twice over so a window is contiguous
        int history_pos_{0};
        int64_t next_output_{0}; // the next output's position, in 1/L input samples, relative to the newest input
    };
}
