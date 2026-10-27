// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "filters.hpp"

#include <cmath>
#include <numbers>

namespace altvane::leveler::dsp {
    std::array<Biquad::Coefficients, 2> kWeighting(const double sample_rate) {
        // ITU-R BS.1770-4 pre-filter, re-derived for any sample rate (the 48 kHz values are the standard's)
        std::array<Biquad::Coefficients, 2> c;
        {
            constexpr double f0 = 1681.974450955533, gain_db = 3.999843853973347, q = .7071752369554196;
            const auto k = std::tan(std::numbers::pi * f0 / sample_rate);
            const auto vh = std::pow(10.0, gain_db / 20.0);
            const auto vb = std::pow(vh, .4996667741545416);
            const auto a0 = 1.0 + k / q + k * k;
            c[0] = {
                (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0
            };
        }
        {
            constexpr double f0 = 38.13547087602444, q = .5003270373238773;
            const auto k = std::tan(std::numbers::pi * f0 / sample_rate);
            const auto a0 = 1.0 + k / q + k * k;
            c[1] = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
        }
        return c;
    }

    namespace {
        // RBJ cookbook
        Biquad::Coefficients pass(const double sample_rate, const double freq, const double q, const bool high) {
            const auto w = 2.0 * std::numbers::pi * freq / sample_rate;
            const auto cw = std::cos(w);
            const auto alpha = std::sin(w) / (2.0 * q);
            const auto a0 = 1.0 + alpha;
            const auto b1 = high ? -(1.0 + cw) : 1.0 - cw;
            const auto b0 = high ? (1.0 + cw) * .5 : (1.0 - cw) * .5;
            return {b0 / a0, b1 / a0, b0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0};
        }
    }

    Biquad::Coefficients lowPass(const double sample_rate, const double freq, const double q) {
        return pass(sample_rate, freq, q, false);
    }

    Biquad::Coefficients highPass(const double sample_rate, const double freq, const double q) {
        return pass(sample_rate, freq, q, true);
    }
}
