// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "level_history.hpp"

namespace altvane::compressor {
    namespace {
        using zlp::CompressController;
        using zldsp::analyzer::MagAnalyzerOps;
        using zldsp::analyzer::MagReceiver;
        using zldsp::analyzer::MagType;
        using zldsp::analyzer::StereoType;

        // never fall further behind the audio than this many columns; older ones are dropped
        constexpr int kMaxColumnsPerUpdate = 64;

        float floorDB(const float db) {
            return std::max(db, LevelHistory::kFloorDB);
        }
    }

    LevelHistory::LevelHistory(PluginProcessor& p) : p_ref_(p) {
        resetGeometry();
    }

    void LevelHistory::setGeometry(const int columns, const float seconds) {
        if (columns == width_ && std::abs(seconds - seconds_) < 1e-3f) {
            return;
        }
        width_ = columns;
        seconds_ = seconds;
        resetGeometry();
    }

    void LevelHistory::resetGeometry() {
        capacity_ = static_cast<size_t>(std::max(width_, 1)) + 4;
        columns_.assign(capacity_, Column{});
        head_ = 0;
        count_ = 0;
        phase_ = 0.f;
        const auto rate = sample_rate_ > 0.0 ? sample_rate_ : 48000.0;
        seconds_per_column_ = seconds_ / static_cast<float>(std::max(width_, 1));
        samples_per_column_ = std::max(16, juce::roundToInt(rate * static_cast<double>(seconds_per_column_)));
        // the column length is rounded to whole samples; keep the time axis exact
        seconds_per_column_ = static_cast<float>(static_cast<double>(samples_per_column_) / rate);
    }

    void LevelHistory::clear() {
        std::fill(columns_.begin(), columns_.end(), Column{});
        count_ = 0;
        phase_ = 0.f;
    }

    void LevelHistory::update() {
        auto& sender = p_ref_.getCompressController().getMagAnalyzerSender();
        // the audio thread only locks this while it prepares; never wait for it
        if (!sender.getLock().try_lock()) {
            return;
        }
        if (std::abs(sample_rate_ - sender.getSampleRate()) > .5 || max_num_samples_ != sender.getMaxNumSamples()) {
            sample_rate_ = sender.getSampleRate();
            max_num_samples_ = sender.getMaxNumSamples();
            transfer_.prepare(sample_rate_, max_num_samples_, {2, 2, 2, 2}, .5);
            transfer_.getMulticastFIFO().removeConsumer(consumer_id_);
            consumer_id_ = transfer_.getMulticastFIFO().addConsumer();
            resetGeometry();
        }
        transfer_.processTransfer(sender.getAbstractFIFO(), sender.getSampleFIFOs());
        sender.getLock().unlock();

        auto& fifo = transfer_.getMulticastFIFO();
        auto& streams = transfer_.getSampleFIFOs();
        const auto mag_type = static_cast<MagType>(p_ref_.na_parameters_.getRawParameterValue(
            zlstate::PAnalyzerMagType::kID)->load(std::memory_order::relaxed) > .5f ? 1 : 0);
        const auto stereo = static_cast<StereoType>(juce::roundToInt(p_ref_.na_parameters_.getRawParameterValue(
            zlstate::PAnalyzerStereo::kID)->load(std::memory_order::relaxed)));

        auto ready = fifo.getNumReady(consumer_id_);
        if (ready > samples_per_column_ * kMaxColumnsPerUpdate) {
            // the editor was hidden or stalled: skip to the recent past instead of replaying it
            const auto skip = ready - samples_per_column_ * 2;
            (void) fifo.prepareToRead(consumer_id_, skip);
            fifo.finishRead(consumer_id_, skip);
            ready -= skip;
        }
        while (ready >= samples_per_column_) {
            const auto range = fifo.prepareToRead(consumer_id_, samples_per_column_);
            const auto& pre = streams[CompressController::kAnalyzerPreStream];
            const auto& compressed = streams[CompressController::kAnalyzerCompressedStream];
            const auto& post = streams[CompressController::kAnalyzerPostStream];
            const auto& side = streams[CompressController::kAnalyzerSideChainStream];

            Column c;
            c.input = floorDB(MagReceiver::calculate(range, pre, mag_type, stereo));
            c.output = floorDB(MagReceiver::calculate(range, post, mag_type, stereo));
            c.side = floorDB(MagReceiver::calculate(range, side, mag_type, stereo));
            c.gain = c.input > kFloorDB + 1.f
                         ? floorDB(MagReceiver::calculate(range, compressed, mag_type, stereo)) - c.input
                         : 0.f;
            head_ = (head_ + 1) % capacity_;
            columns_[head_] = c;
            count_ = std::min(count_ + 1, capacity_);

            // meters are always peak, per channel
            for (size_t ch = 0; ch < 2; ++ch) {
                meters_.input[ch] = std::max(meters_.input[ch],
                                             floorDB(zldsp::chore::gainToDecibels(
                                                 MagAnalyzerOps::calculatePeak(range, pre[ch]))));
                meters_.output[ch] = std::max(meters_.output[ch],
                                              floorDB(zldsp::chore::gainToDecibels(
                                                  MagAnalyzerOps::calculatePeak(range, post[ch]))));
            }
            if (std::abs(c.gain) > std::abs(meters_.gain)) {
                meters_.gain = c.gain;
            }
            meters_.side = std::max(meters_.side, c.side);
            meters_.fresh = true;

            fifo.finishRead(consumer_id_, samples_per_column_);
            ready -= samples_per_column_;
        }
        phase_ = std::clamp(static_cast<float>(ready) / static_cast<float>(samples_per_column_), 0.f, 1.f);
    }

    LevelHistory::Meters LevelHistory::takeMeters() {
        const auto m = meters_;
        meters_ = Meters{};
        return m;
    }
}
