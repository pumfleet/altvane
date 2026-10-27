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
#include <cstdint>
#include <span>

namespace altvane::limiter {
    /**
     * word-length reduction for the output: TPDF dither of ±1 LSB, optionally noise shaped, then rounding to the
     * target bit depth (the samples stay floats, on the bit depth's grid)
     *
     * Shaped feeds the quantisation error back through the 9-tap F-weighted filter of Wannamaker, "Psychoacoustically
     * Optimal Noise Shaping", J. Audio Eng. Soc. 40(7/8), 1992 (the coefficients SoX ships as "f-weighted"). It is
     * designed for 44.1 kHz; it moves the noise out of the 1-5 kHz region where hearing is most sensitive into the
     * top octave. Below 44.1 kHz that top octave is audible, so the dither stays flat there.
     */
    class Dither {
    public:
        enum class Shape { kFlat, kShaped };

        void prepare(const double sample_rate) {
            shaping_allowed_ = sample_rate >= 44100.0 - 1.0;
            reset();
        }

        void reset() {
            for (auto& e : errors_) {
                e.fill(0.0);
            }
        }

        /**
         * @param bits 0 for off, otherwise the target word length (16..24)
         */
        void setBits(const int bits) {
            if (bits != bits_) {
                bits_ = bits;
                reset();
            }
        }

        void setShape(const Shape shape) {
            if (shape != shape_) {
                shape_ = shape;
                reset();
            }
        }

        [[nodiscard]] bool isOn() const { return bits_ > 0; }

        void process(std::span<float*> channels, const size_t num_samples) {
            if (bits_ <= 0) {
                return;
            }
            const auto scale = std::ldexp(1.0, bits_ - 1);
            const auto lsb = 1.0 / scale;
            const auto largest = 1.0 - lsb;
            const auto shaped = shape_ == Shape::kShaped && shaping_allowed_;
            for (size_t c = 0; c < std::min(channels.size(), errors_.size()); ++c) {
                auto* x = channels[c];
                auto& e = errors_[c];
                for (size_t n = 0; n < num_samples; ++n) {
                    // the wanted value, less the filtered past errors: the noise then has the spectrum 1 - H(z)
                    auto wanted = static_cast<double>(x[n]);
                    if (shaped) {
                        double feedback = 0.0;
                        for (size_t k = 0; k < kTaps; ++k) {
                            feedback += kShaping[k] * e[k];
                        }
                        wanted -= feedback;
                    }
                    const auto dither = (uniform() + uniform() - 1.0) * lsb;
                    const auto quantised = std::round((wanted + dither) * scale) * lsb;
                    if (shaped) {
                        std::copy_backward(e.begin(), e.end() - 1, e.end());
                        // the error is bounded by 1.5 LSB, so the feedback cannot run away
                        e[0] = quantised - wanted;
                    }
                    x[n] = static_cast<float>(std::clamp(quantised, -1.0, largest));
                }
            }
        }

    private:
        static constexpr size_t kTaps = 9;
        static constexpr std::array<double, kTaps> kShaping{
            2.412, -3.370, 3.937, -4.174, 3.353, -2.205, 1.281, -0.569, 0.0847
        };

        int bits_{0};
        Shape shape_{Shape::kShaped};
        bool shaping_allowed_{true};
        std::array<std::array<double, kTaps>, 2> errors_{};
        uint64_t state_{0x9E3779B97F4A7C15ull};

        // xorshift64*: uniform in [0, 1)
        double uniform() {
            state_ ^= state_ >> 12;
            state_ ^= state_ << 25;
            state_ ^= state_ >> 27;
            return static_cast<double>((state_ * 0x2545F4914F6CDD1Dull) >> 11) * 0x1.0p-53;
        }
    };
}
