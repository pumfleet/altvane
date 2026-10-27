// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <vector>

#include "filters.hpp"

namespace altvane::deesser::dsp {
    /**
     * from overshoot to gain reduction: a soft knee, a ceiling at the range, a look-ahead window (the largest
     * reduction wanted anywhere in the next `lookahead` samples, so it is in place when the ess arrives) and
     * program-dependent smoothing (short esses release faster than long ones)
     */
    class GainComputer {
    public:
        static constexpr float kKneeDb = 6.f;

        void prepare(const double sample_rate, const int max_lookahead) {
            sample_rate_ = sample_rate;
            capacity_ = static_cast<size_t>(max_lookahead) + 2;
            times_.assign(capacity_, 0);
            values_.assign(capacity_, 0.f);
            reset();
        }

        void reset() {
            head_ = 0;
            size_ = 0;
            time_ = 0;
            reduction_ = 0.f;
            event_seconds_ = 0.f;
        }

        /**
         * the reduction (dB, positive) wanted for a band level `over` dB above the threshold
         */
        static float target(const float over, const float sibilance, const float range) {
            float r;
            if (over <= -.5f * kKneeDb) {
                return 0.f;
            }
            if (over < .5f * kKneeDb) {
                const auto t = over + .5f * kKneeDb;
                r = t * t / (2.f * kKneeDb);
            } else {
                r = over;
            }
            return std::min(r * sibilance, range);
        }

        /**
         * set once per control period
         */
        void setTiming(const int lookahead) {
            lookahead_ = lookahead;
            // the attack spans the look-ahead (95% by the time the ess arrives), never faster than 0.3 ms
            attack_ = onePole(sample_rate_, std::max(.0003, static_cast<double>(lookahead) / sample_rate_ / 3.0));
            // 40 ms after a short ess, up to 120 ms after a long one
            const auto t = std::clamp(event_seconds_ / .25f, 0.f, 1.f);
            release_ = onePole(sample_rate_, .040 + .080 * t);
        }

        float process(const float wanted) {
            // running maximum over the window: a monotonic queue in a fixed ring
            while (size_ > 0 && values_[back()] <= wanted) {
                --size_;
            }
            const auto slot = (head_ + size_) % capacity_;
            times_[slot] = time_;
            values_[slot] = wanted;
            ++size_;
            while (times_[head_] + lookahead_ < time_) {
                head_ = (head_ + 1) % capacity_;
                --size_;
            }
            ++time_;
            const auto peak = values_[head_];

            reduction_ += (peak - reduction_) * (peak > reduction_ ? attack_ : release_);
            if (reduction_ < 1e-4f) {
                // settles to exactly nothing, so the output is the input again
                reduction_ = 0.f;
            }
            event_seconds_ = peak > .5f ? event_seconds_ + 1.f / static_cast<float>(sample_rate_) : 0.f;
            return reduction_;
        }

        float getReduction() const { return reduction_; }

    private:
        double sample_rate_{48000.0};
        size_t capacity_{2}, head_{0}, size_{0};
        int64_t time_{0};
        int lookahead_{0};
        std::vector<int64_t> times_;
        std::vector<float> values_;
        float reduction_{0.f}, attack_{1.f}, release_{1.f}, event_seconds_{0.f};

        size_t back() const { return (head_ + size_ - 1) % capacity_; }
    };
}
