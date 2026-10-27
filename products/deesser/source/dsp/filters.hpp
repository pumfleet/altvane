// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace altvane::deesser::dsp {
    /**
     * coefficients of a topology-preserving state-variable filter (A. Simper, "Linear trapezoidal integrated SVF")
     */
    struct SVFCoeffs {
        float k{1.4142135f}, a1{1.f}, a2{0.f}, a3{0.f};

        static SVFCoeffs make(double hz, const double q, const double sample_rate) {
            hz = std::clamp(hz, 10.0, .45 * sample_rate);
            return fromG(static_cast<float>(std::tan(std::numbers::pi * hz / sample_rate)), static_cast<float>(1.0 / q));
        }

        /**
         * from the prewarped frequency g = tan(pi f / fs) and damping k = 1 / q (cheap enough per sample)
         */
        static SVFCoeffs fromG(const float g, const float k) {
            SVFCoeffs c;
            c.k = k;
            c.a1 = 1.f / (1.f + g * (g + k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
            return c;
        }
    };

    /**
     * one state-variable filter, all three outputs at once; bp has a peak gain of q (k * bp is unity at the centre)
     */
    class SVF {
    public:
        struct Out {
            float lp, bp, hp;
        };

        Out process(const float x, const SVFCoeffs& c) {
            const auto v3 = x - ic2_;
            const auto v1 = c.a1 * ic1_ + c.a2 * v3;
            const auto v2 = ic2_ + c.a2 * ic1_ + c.a3 * v3;
            ic1_ = 2.f * v1 - ic1_;
            ic2_ = 2.f * v2 - ic2_;
            return {v2, v1, x - c.k * v1 - v2};
        }

        void reset() {
            ic1_ = 0.f;
            ic2_ = 0.f;
        }

    private:
        float ic1_{0.f}, ic2_{0.f};
    };

    // the two sections of a fourth-order Butterworth
    inline constexpr double kButterworth4Q1 = .5411961;
    inline constexpr double kButterworth4Q2 = 1.3065630;
    inline constexpr double kButterworth2Q = .70710678;

    /**
     * a value that moves to its target in a straight line and lands on it exactly (so a settled mix is bit-exact)
     */
    class Ramp {
    public:
        void reset(const double sample_rate, const double seconds) {
            step_ = static_cast<float>(1.0 / std::max(1.0, sample_rate * seconds));
        }

        void snap(const float v) {
            value_ = v;
            target_ = v;
        }

        void setTarget(const float v) { target_ = v; }

        float next() {
            if (value_ < target_) {
                value_ = std::min(target_, value_ + step_);
            } else if (value_ > target_) {
                value_ = std::max(target_, value_ - step_);
            }
            return value_;
        }

        float get() const { return value_; }

    private:
        float value_{0.f}, target_{0.f}, step_{1.f};
    };

    inline float onePole(const double sample_rate, const double seconds) {
        return seconds <= 0.0 ? 1.f : static_cast<float>(1.0 - std::exp(-1.0 / (sample_rate * seconds)));
    }

    inline float gainToDb(const float gain) {
        return gain > 1e-6f ? 20.f * std::log10(gain) : -120.f;
    }

    inline float dbToGain(const float db) {
        return std::pow(10.f, db * .05f);
    }
}
