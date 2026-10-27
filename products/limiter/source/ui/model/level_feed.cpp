// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "level_feed.hpp"

#include <cmath>
#include <mutex>

#include "../../PluginProcessor.hpp"

namespace altvane::limiter {
    namespace {
        float toDB(const float gain) {
            return gain > 1e-6f ? 20.f * std::log10(gain) : LevelFeed::kFloor;
        }
    }

    LevelFeed::LevelFeed(PluginProcessor& p) : p_ref_(p), columns_(kCapacity) {
        output_receiver_.prepare(2);
        input_receiver_.prepare(2);
        clear();
    }

    LevelFeed::~LevelFeed() {
        setActive(false);
    }

    void LevelFeed::setActive(const bool active) {
        p_ref_.getController().setAnalyzerEnabled(active);
    }

    void LevelFeed::clear() {
        std::fill(columns_.begin(), columns_.end(), LevelColumn{kFloor, kFloor, 0.f, kFloor});
        head_ = 0;
        count_ = 0;
        column_filled_ = 0;
        column_input_ = column_output_ = 0.f;
        column_reduction_ = 0.f;
        reduction_receiver_.reset();
        output_receiver_.reset();
        input_receiver_.reset();
    }

    void LevelFeed::push(const float loudness) {
        // the meter publishes ten times a second: glide between readings instead of drawing steps
        if (loudness <= LoudnessReadings::kNoLevel || loudness_ <= LoudnessReadings::kNoLevel) {
            loudness_ = loudness;
        } else {
            loudness_ += .08f * (loudness - loudness_);
        }
        head_ = (head_ + 1) % kCapacity;
        columns_[head_] = {toDB(column_input_), toDB(column_output_), column_reduction_, loudness_};
        count_ = std::min(count_ + 1, kCapacity);
        column_filled_ = 0;
        column_input_ = column_output_ = 0.f;
        column_reduction_ = 0.f;
    }

    size_t LevelFeed::pull(const float loudness, const bool true_peak) {
        auto& controller = p_ref_.getController();
        auto& sender = controller.getMagAnalyzerSender();
        output_peaks = {kFloor, kFloor};
        input_peaks = {kFloor, kFloor};
        reduction = 0.f;
        has_new = false;

        const std::lock_guard guard(sender.getLock());
        if (sender.getSampleRate() <= 0.0 || sender.getMaxNumSamples() == 0) {
            return 0;
        }
        auto& fifo = sender.getAbstractFIFO();
        if (const auto generation = controller.getAnalyzerGeneration(); generation != generation_) {
            // the engine was prepared or reset: what is in the FIFO no longer lines up
            generation_ = generation;
            fifo.finishRead(fifo.getNumReady());
            sample_rate_ = sender.getSampleRate();
            column_samples_ = static_cast<size_t>(std::max(1.0, std::round(sample_rate_ / kColumnsPerSecond)));
            reduction_receiver_.reset();
            output_receiver_.reset();
            input_receiver_.reset();
            stereo_receiver_.prepare(sample_rate_, 2);
            correlation = std::numeric_limits<float>::quiet_NaN();
            column_filled_ = 0;
            return 0;
        }
        const auto ready = fifo.getNumReady();
        if (ready <= 0) {
            return 0;
        }
        const auto range = fifo.prepareToRead(ready);
        auto& streams = sender.getSampleFIFOs();
        const auto& gained = streams[zlp::Controller::kAnalyzerGainedPreStream];
        const auto& limited = streams[zlp::Controller::kAnalyzerLimiterPostStream];
        const auto& post = streams[zlp::Controller::kAnalyzerPostStream];

        output_receiver_.run(range, post, true_peak);
        input_receiver_.run(range, gained, true_peak);
        stereo_receiver_.run(range, post);
        correlation = stereo_receiver_.getCorrelation();
        output_peaks = output_receiver_.getDBs();
        input_peaks = input_receiver_.getDBs();
        has_new = true;

        size_t columns = 0;
        const auto walk = [&](const int start, const int count) {
            auto i = static_cast<size_t>(start);
            auto left = static_cast<size_t>(count);
            while (left > 0) {
                const auto n = std::min(left, column_samples_ - column_filled_);
                for (size_t k = i; k < i + n; ++k) {
                    column_input_ = std::max({column_input_, std::abs(gained[0][k]), std::abs(gained[1][k])});
                    column_output_ = std::max({column_output_, std::abs(post[0][k]), std::abs(post[1][k])});
                }
                reduction_receiver_.run({static_cast<int>(i), static_cast<int>(n), 0, 0}, gained, limited);
                column_reduction_ = std::min(column_reduction_, reduction_receiver_.getMinDB());
                reduction = std::min(reduction, reduction_receiver_.getMinDB());
                column_filled_ += n;
                i += n;
                left -= n;
                if (column_filled_ == column_samples_) {
                    push(loudness);
                    ++columns;
                }
            }
        };
        walk(range.start_index1, range.block_size1);
        walk(range.start_index2, range.block_size2);
        fifo.finishRead(ready);
        return columns;
    }
}
