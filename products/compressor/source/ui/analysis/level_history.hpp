// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <vector>

#include "../../PluginProcessor.hpp"
#include "../../dsp/analyzer/analyzer_base/fifo_transfer_buffer.hpp"
#include "../../dsp/analyzer/mag_analyzer/mag_receiver.hpp"

namespace altvane::compressor {
    /**
     * the level history behind the display and the meters, read from the engine's magnitude analyzer
     * (input, compressed before makeup, output, side chain after its EQ)
     *
     * the audio is cut into columns of equal length; each column stores the input, output and side-chain levels
     * and the gain change (compressed − input), as upstream's interface measured it
     * runs on the message thread once per frame: a column is a few hundred samples, so this is cheap
     */
    class LevelHistory final {
    public:
        static constexpr float kFloorDB = -120.f;

        struct Column {
            float input{kFloorDB}, output{kFloorDB}, side{kFloorDB}, gain{0.f};
        };

        /**
         * the loudest values since the last takeMeters()
         */
        struct Meters {
            std::array<float, 2> input{kFloorDB, kFloorDB}, output{kFloorDB, kFloorDB};
            // the largest gain change, signed
            float gain{0.f};
            float side{kFloorDB};
            bool fresh{false};
        };

        explicit LevelHistory(PluginProcessor& p);

        /**
         * one column per pixel: the history covers seconds over columns pixels
         */
        void setGeometry(int columns, float seconds);

        /**
         * pull what the engine has produced since the last call
         */
        void update();

        size_t size() const { return count_; }

        /**
         * @param ago 0 is the newest column
         */
        const Column& get(const size_t ago) const {
            return columns_[(head_ + capacity_ - ago) % capacity_];
        }

        /**
         * how much of the next column has arrived, 0 .. 1, for smooth scrolling
         */
        float getPhase() const { return phase_; }

        float getSecondsPerColumn() const { return seconds_per_column_; }

        Meters takeMeters();

        void clear();

    private:
        using Transfer = zldsp::analyzer::FIFOTransferBuffer<zlp::CompressController::kAnalyzerStreamNum>;

        PluginProcessor& p_ref_;
        Transfer transfer_;
        size_t consumer_id_{0};
        double sample_rate_{0.0};
        size_t max_num_samples_{0};

        std::vector<Column> columns_;
        size_t capacity_{1}, head_{0}, count_{0};
        int samples_per_column_{480};
        int width_{0};
        float seconds_{9.f}, seconds_per_column_{.01f}, phase_{0.f};
        Meters meters_;

        void resetGeometry();
    };
}
