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
#include <vector>

#include "../../engine/loudness_meter.hpp"

#include "../../dsp/analyzer/mag_analyzer/limit_reduction_receiver.hpp"
#include "../../dsp/analyzer/mag_analyzer/magnitude_receiver.hpp"
#include "../../dsp/analyzer/value_analyzer/stereo_statistics_receiver.hpp"

class PluginProcessor;

namespace altvane::limiter {
    /**
     * one 10 ms slice of the level history
     */
    struct LevelColumn {
        // peaks of the gained input and of what is heard (the output, the dry input when bypassed, the removed
        // signal in Audition), dBFS
        float input, output;
        // gain reduction, dB (<= 0)
        float reduction;
        // short-term loudness when the slice arrived, LUFS
        float loudness;
    };

    /**
     * reads the engine's analyzer streams on the message thread, once per frame: a scrolling history of 10 ms
     * columns for the display, and the peaks since the last frame for the meters
     */
    class LevelFeed {
    public:
        static constexpr double kColumnsPerSecond = 100.0;
        // a little more than the longest history (18 s), so the display can scroll in from the edge
        static constexpr size_t kCapacity = 2000;
        static constexpr float kFloor = -120.f;

        explicit LevelFeed(PluginProcessor& p);

        ~LevelFeed();

        /**
         * start or stop the engine's analyzer (it costs nothing while the editor is closed)
         */
        void setActive(bool active);

        /**
         * take everything the engine produced since the last call
         * @param loudness the current short-term loudness, stored with the new columns
         * @param true_peak whether the meters show true peaks rather than sample peaks
         * @return the number of new columns
         */
        size_t pull(float loudness, bool true_peak);

        size_t size() const { return count_; }

        /**
         * a column, 0 the newest
         */
        const LevelColumn& ago(const size_t i) const {
            return columns_[(head_ + kCapacity - i) % kCapacity];
        }

        void clear();

        // since the last pull, for the meters (dB); the reduction is the deepest
        std::array<float, 2> output_peaks{kFloor, kFloor}, input_peaks{kFloor, kFloor};
        float reduction{0.f};
        bool has_new{false};
        // the output's left / right correlation over the last 400 ms (NaN until measured)
        float correlation{std::numeric_limits<float>::quiet_NaN()};

    private:
        PluginProcessor& p_ref_;
        uint64_t generation_{~uint64_t{0}};
        std::vector<LevelColumn> columns_;
        size_t head_{0}, count_{0};

        zldsp::analyzer::LimitReductionReceiver reduction_receiver_;
        zldsp::analyzer::MagnitudeReceiver output_receiver_, input_receiver_;
        zldsp::analyzer::StereoStatisticsReceiver stereo_receiver_;
        // the audible output in Audition (gained minus limited), indexed like the engine's FIFO
        std::vector<std::vector<float>> audible_{2};
        size_t column_samples_{480}, column_filled_{0};
        float column_input_{0.f}, column_output_{0.f}, column_reduction_{0.f};
        float loudness_{kFloor};
        double sample_rate_{0.0};

        void push(float loudness);
    };
}
