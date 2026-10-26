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
     * user preferences that affect motion and input, applied globally
     */
    struct InteractionSettings {
        static inline bool reduce_motion = false;
        static inline float drag_sensitivity = 1.f;
        static inline float wheel_sensitivity = 1.f;
    };

    /**
     * a critically-damped (by default) second-order smoother for UI motion
     * stable for any frame time, never overshoots when damping >= 1
     */
    class Spring {
    public:
        explicit Spring(const float initial = 0.f, const float frequency_hz = 6.f, const float damping = 1.f) :
            value_(initial), target_(initial), omega_(2.f * 3.14159265f * frequency_hz), zeta_(damping) {
        }

        void setTarget(const float target) { target_ = target; }

        void snap(const float value) {
            value_ = value;
            target_ = value;
            velocity_ = 0.f;
        }

        float getValue() const { return value_; }

        float getTarget() const { return target_; }

        bool isSettled() const {
            return std::abs(value_ - target_) < 1e-3f && std::abs(velocity_) < 1e-3f;
        }

        /**
         * advance by dt seconds, returns true while still moving
         */
        bool update(float dt) {
            if (InteractionSettings::reduce_motion && !isSettled()) {
                value_ = target_;
                velocity_ = 0.f;
                return true;
            }
            if (isSettled()) {
                value_ = target_;
                velocity_ = 0.f;
                return false;
            }
            dt = std::min(dt, 0.05f);
            // sub-step for stability with large frame gaps
            constexpr int kSteps = 4;
            const auto h = dt / static_cast<float>(kSteps);
            for (int i = 0; i < kSteps; ++i) {
                const auto accel = omega_ * omega_ * (target_ - value_) - 2.f * zeta_ * omega_ * velocity_;
                velocity_ += accel * h;
                value_ += velocity_ * h;
            }
            return true;
        }

    private:
        float value_, target_, velocity_{0.f};
        float omega_, zeta_;
    };

    /**
     * a simple linear-time fade used for opacity transitions
     */
    class Fade {
    public:
        explicit Fade(const float duration_s = .16f) : duration_(duration_s) {
        }

        void setTarget(const bool on) { target_ = on ? 1.f : 0.f; }

        void snap(const bool on) {
            target_ = on ? 1.f : 0.f;
            value_ = target_;
        }

        float getValue() const { return value_; }

        /**
         * eased value (cubic ease-out)
         */
        float getEased() const {
            const auto t = 1.f - value_;
            return 1.f - t * t * t;
        }

        bool update(const float dt) {
            if (InteractionSettings::reduce_motion && std::abs(value_ - target_) >= 1e-6f) {
                value_ = target_;
                return true;
            }
            if (std::abs(value_ - target_) < 1e-6f) {
                value_ = target_;
                return false;
            }
            const auto step = dt / duration_;
            value_ = target_ > value_ ? std::min(target_, value_ + step) : std::max(target_, value_ - step);
            return true;
        }

    private:
        float duration_;
        float value_{0.f}, target_{0.f};
    };
}
