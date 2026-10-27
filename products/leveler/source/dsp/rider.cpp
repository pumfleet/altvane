// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "rider.hpp"

namespace altvane::leveler::dsp {
    void Rider::prepare(const double hop_seconds) {
        hop_ = hop_seconds;
    }

    void Rider::reset(const float gain_db) {
        gain_ = gain_db;
        silent_time_ = 1e9;
        onset_left_ = 0.0;
    }

    double Rider::boostTime(const float speed) {
        return 1.5 * std::pow(.04 / 1.5, static_cast<double>(std::clamp(speed, 0.f, 1.f)));
    }

    void Rider::moveToward(const float desired, const double time, const float scale) {
        const auto coefficient = 1.0 - std::exp(-hop_ / std::max(time, 1e-4));
        gain_ += (desired - gain_) * static_cast<float>(coefficient) * scale;
    }

    float Rider::update(const HopState state, const float voiced_db, const float level_db, const Settings& s) {
        const auto lo = std::min(s.range_min, s.range_max);
        const auto hi = std::max(s.range_min, s.range_max);
        const auto boost = boostTime(s.speed);
        const auto cut = boost * .5;
        auto ride = [&](const float level, const float scale, const bool onset) {
            const auto desired = std::clamp(s.target_db - level, lo, hi);
            auto time = desired > gain_ ? boost : cut;
            if (onset) {
                time = std::min(time, kOnsetTime);
            }
            moveToward(desired, time, scale);
        };

        switch (state) {
            case HopState::kVoiced: {
                if (silent_time_ > kHoldSeconds) {
                    onset_left_ = kOnsetSeconds;
                }
                silent_time_ = 0.0;
                const auto onset = onset_left_ > 0.0;
                onset_left_ = std::max(onset_left_ - hop_, 0.0);
                ride(voiced_db, 1.f, onset);
                break;
            }
            case HopState::kUnvoiced: {
                // breaths, sibilants and noise: the guard slows them down to nothing
                const auto g = 1.f - std::clamp(s.guard, 0.f, 1.f);
                if (g > 0.f) {
                    ride(level_db, g * g * g, false);
                }
                break;
            }
            case HopState::kSilent: {
                silent_time_ += hop_;
                onset_left_ = 0.0;
                if (silent_time_ > kHoldSeconds) {
                    moveToward(std::clamp(s.idle_db, lo, hi), std::max(2.0 * boost, .3));
                }
                break;
            }
        }
        gain_ = std::clamp(gain_, lo, hi);
        return gain_;
    }
}
