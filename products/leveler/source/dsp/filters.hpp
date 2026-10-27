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

namespace altvane::leveler::dsp {
    /**
     * a second-order section in transposed direct form II, double precision so the 38 Hz high-pass of the
     * K-weighting stays exact at 192 kHz
     */
    class Biquad {
    public:
        struct Coefficients {
            double b0{1.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
        };

        void setCoefficients(const Coefficients& c) { c_ = c; }

        void reset() { s1_ = s2_ = 0.0; }

        double process(const double x) {
            const auto y = c_.b0 * x + s1_;
            s1_ = c_.b1 * x - c_.a1 * y + s2_;
            s2_ = c_.b2 * x - c_.a2 * y;
            return y;
        }

    private:
        Coefficients c_;
        double s1_{0.0}, s2_{0.0};
    };

    /**
     * the BS.1770 pre-filter (head shelf, then the RLB high-pass) for a sample rate
     */
    std::array<Biquad::Coefficients, 2> kWeighting(double sample_rate);

    Biquad::Coefficients lowPass(double sample_rate, double freq, double q);

    Biquad::Coefficients highPass(double sample_rate, double freq, double q);

    /**
     * K-weighting for one channel
     */
    class KFilter {
    public:
        void prepare(const double sample_rate) {
            const auto c = kWeighting(sample_rate);
            shelf_.setCoefficients(c[0]);
            high_pass_.setCoefficients(c[1]);
            reset();
        }

        void reset() {
            shelf_.reset();
            high_pass_.reset();
        }

        double process(const double x) { return high_pass_.process(shelf_.process(x)); }

    private:
        Biquad shelf_, high_pass_;
    };
}
