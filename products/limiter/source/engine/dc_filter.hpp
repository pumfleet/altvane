// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

namespace altvane::limiter {
    /**
     * removes DC offset before the limiter, so an offset does not use up headroom: a first-order high-pass at 5 Hz
     *
     * the filtered signal differs from the input by the offset and by the low end's phase, so turning the filter on or
     * off (bypass included) cross-fades between the two over 50 ms instead of stepping
     */
    class DCFilter {
    public:
        static constexpr double kCutoffHz = 5.0;
        static constexpr double kFadeSeconds = .05;

        void prepare(const double sample_rate) {
            pole_ = std::exp(-2.0 * std::numbers::pi * kCutoffHz / sample_rate);
            fade_length_ = std::max(1, static_cast<int>(std::lround(kFadeSeconds * sample_rate)));
            reset();
        }

        /**
         * start again from the input: the filter fades in from a clear state if it is on
         */
        void reset() {
            x1_.fill(0.0);
            y1_.fill(0.0);
            fade_ = 0;
        }

        void setOn(const bool on) { on_ = on; }

        void process(std::span<float*> channels, const size_t num_samples) {
            if (fade_ == 0) {
                if (!on_) {
                    return; // off: the input passes untouched
                }
                // fading in from off: from a clear state, whose first output is the input itself
                x1_.fill(0.0);
                y1_.fill(0.0);
            }
            const auto step = on_ ? 1 : -1;
            const auto scale = 1.0 / static_cast<double>(fade_length_);
            auto end_fade = fade_;
            for (size_t c = 0; c < std::min(channels.size(), x1_.size()); ++c) {
                auto* x = channels[c];
                auto x1 = x1_[c], y1 = y1_[c];
                auto fade = fade_;
                for (size_t n = 0; n < num_samples; ++n) {
                    const auto in = static_cast<double>(x[n]);
                    y1 = in - x1 + pole_ * y1;
                    x1 = in;
                    fade = std::clamp(fade + step, 0, fade_length_);
                    x[n] = static_cast<float>(fade == fade_length_ ? y1 : in + scale * fade * (y1 - in));
                }
                // keep denormals out of the recursion once the input goes silent
                x1_[c] = x1;
                y1_[c] = std::abs(y1) < 1e-30 ? 0.0 : y1;
                end_fade = fade;
            }
            fade_ = end_fade;
        }

    private:
        double pole_{.9993};
        // how far the fade from the input to the filtered signal has gone, 0 to fade_length_ samples
        int fade_length_{2400}, fade_{0};
        bool on_{false};
        std::array<double, 2> x1_{}, y1_{};
    };
}
