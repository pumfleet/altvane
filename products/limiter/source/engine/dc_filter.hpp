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
#include <numbers>
#include <span>

namespace altvane::limiter {
    /**
     * removes DC offset before the limiter, so an offset does not use up headroom: a first-order high-pass at 5 Hz
     */
    class DCFilter {
    public:
        static constexpr double kCutoffHz = 5.0;

        void prepare(const double sample_rate) {
            pole_ = std::exp(-2.0 * std::numbers::pi * kCutoffHz / sample_rate);
            reset();
        }

        void reset() {
            x1_.fill(0.0);
            y1_.fill(0.0);
        }

        void process(std::span<float*> channels, const size_t num_samples) {
            for (size_t c = 0; c < std::min(channels.size(), x1_.size()); ++c) {
                auto* x = channels[c];
                auto x1 = x1_[c], y1 = y1_[c];
                for (size_t n = 0; n < num_samples; ++n) {
                    const auto in = static_cast<double>(x[n]);
                    y1 = in - x1 + pole_ * y1;
                    x1 = in;
                    x[n] = static_cast<float>(y1);
                }
                // keep denormals out of the recursion once the input goes silent
                x1_[c] = x1;
                y1_[c] = std::abs(y1) < 1e-30 ? 0.0 : y1;
            }
        }

    private:
        double pole_{.9993};
        std::array<double, 2> x1_{}, y1_{};
    };
}
