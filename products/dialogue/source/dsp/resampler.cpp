// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "resampler.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace altvane::dialogue::dsp {
    namespace {
        double besselI0(const double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 64; ++k) {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
                if (term < sum * 1e-12) {
                    break;
                }
            }
            return sum;
        }
    }

    int64_t Resampler::commonRate(const int a, const int b) {
        return std::lcm(static_cast<int64_t>(a), static_cast<int64_t>(b));
    }

    void Resampler::prepare(const int from, const int to, const int prototype_length, const double cutoff) {
        const auto rate = commonRate(from, to);
        up_ = rate / from;
        down_ = rate / to;
        taps_ = static_cast<int>((prototype_length + up_ - 1) / up_);

        // windowed-sinc prototype at the common rate, with a gain of L to make up for the inserted zeros
        constexpr double kBeta = 8.0;
        const auto fc = cutoff / static_cast<double>(rate);
        const auto centre = (prototype_length - 1) * .5;
        const auto norm = besselI0(kBeta);
        coefficients_.assign(static_cast<size_t>(up_ * taps_), 0.f);
        for (int64_t phase = 0; phase < up_; ++phase) {
            for (int j = 0; j < taps_; ++j) {
                const auto k = phase + static_cast<int64_t>(j) * up_;
                if (k >= prototype_length) {
                    continue;
                }
                const auto t = static_cast<double>(k) - centre;
                const auto sinc = t == 0.0 ? 1.0 : std::sin(2.0 * std::numbers::pi * fc * t) / (std::numbers::pi * t * 2.0 * fc);
                const auto r = t / (centre > 0.0 ? centre : 1.0);
                const auto window = besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / norm;
                coefficients_[static_cast<size_t>(phase * taps_ + j)] =
                    static_cast<float>(2.0 * fc * sinc * window * static_cast<double>(up_));
            }
        }
        history_.assign(static_cast<size_t>(2 * taps_), 0.f);
        reset();
    }

    void Resampler::reset() {
        std::fill(history_.begin(), history_.end(), 0.f);
        history_pos_ = 0;
        next_output_ = up_;
    }

    int Resampler::maxOutput(const int n) const {
        return static_cast<int>((static_cast<int64_t>(n) * up_ + down_ - 1) / down_) + 2;
    }

    int Resampler::process(const float* in, const int n, float* out) {
        int produced = 0;
        for (int i = 0; i < n; ++i) {
            // newest input first: the filter window is history_[pos .. pos + taps)
            history_pos_ = history_pos_ == 0 ? taps_ - 1 : history_pos_ - 1;
            history_[static_cast<size_t>(history_pos_)] = in[i];
            history_[static_cast<size_t>(history_pos_ + taps_)] = in[i];
            next_output_ -= up_;
            // every output whose position is now covered: 0 <= position < L after the newest input
            while (next_output_ < up_) {
                const auto* c = coefficients_.data() + next_output_ * taps_;
                const auto* x = history_.data() + history_pos_;
                float y = 0.f;
                for (int j = 0; j < taps_; ++j) {
                    y += c[j] * x[j];
                }
                out[produced++] = y;
                next_output_ += down_;
            }
        }
        return produced;
    }
}
