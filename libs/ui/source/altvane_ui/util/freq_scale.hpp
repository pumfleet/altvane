// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <algorithm>
#include <cmath>

namespace altvane::ui {
    /**
     * maps between frequency / gain and graph coordinates
     * the frequency axis is a fixed 10 Hz - 30 kHz log scale, independent of the sample rate
     */
    class FreqScale {
    public:
        static constexpr double kMinFreq = 10.0;
        static constexpr double kMaxFreq = 30000.0;

        void setBounds(const float x, const float width) {
            x_ = x;
            width_ = std::max(width, 1.f);
        }

        float freqToX(const double freq) const {
            const auto p = std::log(std::max(freq, 1e-3) / kMinFreq) * kInvLogSpan;
            return x_ + static_cast<float>(p) * width_;
        }

        double xToFreq(const float x) const {
            const auto p = static_cast<double>((x - x_) / width_);
            return kMinFreq * std::exp(p * kLogSpan);
        }

        float getX() const { return x_; }
        float getWidth() const { return width_; }

        /**
         * frequency of the i-th of n evenly spaced (in log) points across the axis
         */
        static double pointToFreq(const size_t i, const size_t n) {
            const auto p = static_cast<double>(i) / static_cast<double>(std::max<size_t>(n - 1, 1));
            return kMinFreq * std::exp(p * kLogSpan);
        }

    private:
        static inline const double kLogSpan = std::log(kMaxFreq / kMinFreq);
        static inline const double kInvLogSpan = 1.0 / std::log(kMaxFreq / kMinFreq);
        float x_{0.f}, width_{1.f};
    };

    /**
     * linear dB axis, symmetric around 0 dB
     */
    class GainScale {
    public:
        void setBounds(const float y, const float height, const float padding) {
            centre_ = y + height * .5f;
            half_ = std::max(height * .5f - padding, 1.f);
        }

        void setMaxDB(const float max_db) {
            max_db_ = std::max(max_db, .1f);
        }

        float getMaxDB() const { return max_db_; }

        float dbToY(const float db) const {
            return centre_ - db / max_db_ * half_;
        }

        float yToDB(const float y) const {
            return (centre_ - y) / half_ * max_db_;
        }

        float getCentreY() const { return centre_; }
        float getHalfHeight() const { return half_; }

    private:
        float centre_{0.f}, half_{1.f}, max_db_{12.f};
    };
}
