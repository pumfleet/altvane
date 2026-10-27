// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <atomic>
#include <optional>
#include <vector>
#include <altvane_ui/util/spsc_queue.hpp>

#include "common.hpp"
#include "filters.hpp"
#include "rider.hpp"
#include "voicing.hpp"

namespace altvane::leveler::dsp {
    /**
     * what the processor tells the engine; written by the processor every block, read once per hop
     */
    struct Controls {
        std::atomic<float> target{-20.f}, sensitivity{0.f}, speed{.5f}, guard{.7f}, music{0.f};
        std::atomic<float> range_min{-6.f}, range_max{6.f}, idle{0.f};
        // only for the history display; the processor applies the output gain
        std::atomic<float> output{0.f};
        std::atomic<bool> lookahead{true};
        std::atomic<int> mode{static_cast<int>(Mode::kOff)};
        // the rider fader parameter, followed in Read mode
        std::atomic<float> ride{0.f};
        // a hand on the rider fader overrides everything
        std::atomic<bool> manual{false};
        std::atomic<float> manual_gain{0.f};
    };

    /**
     * the Leveler engine: K-weighted levels and voicing on the input as it arrives, the rider, and the audio
     * delayed by the lookahead so a gain move can land before the phrase does
     */
    class Engine {
    public:
        static constexpr size_t kMaxChannels = 2;
        // a little over 10 s of history at one column per hop
        static constexpr size_t kColumns = 2048;

        void prepare(double sample_rate, int max_block);

        void reset();

        /**
         * process up to max_block samples in place; the key may be null
         */
        void process(float* const* main, int channels, const float* const* key, int key_channels, int n);

        /**
         * the input as it left the delay in the last process call (the dry signal for bypass)
         */
        const float* getDelayedDry(const int channel) const { return dry_[static_cast<size_t>(channel)].data(); }

        Controls& getControls() { return controls_; }

        const Controls& getControls() const { return controls_; }

        /**
         * the latency for the current lookahead setting, in samples
         */
        int getLatency() const;

        int getHopSamples() const { return hop_; }

        /**
         * the gain of the latest hop, in dB (any thread)
         */
        float getGainDb() const { return gain_db_.load(std::memory_order::relaxed); }

        HopState getState() const { return static_cast<HopState>(state_.load(std::memory_order::relaxed)); }

        /**
         * history columns are only queued while the editor shows them
         */
        void setPublishing(bool publishing);

        bool popColumn(Column& column) { return columns_.pop(column); }

        /**
         * measure the voiced loudness from now until stopLearn
         */
        void startLearn();

        /**
         * the voiced loudness heard since startLearn, if there was enough voice
         */
        std::optional<float> stopLearn();

        bool isLearning() const { return learning_.load(std::memory_order::relaxed); }

    private:
        static constexpr size_t kLevelHops = 20; // 100 ms
        static constexpr size_t kFastHops = 4; // 20 ms
        static constexpr size_t kKeyHops = 80; // 400 ms
        static constexpr size_t kRecordHops = 8;
        static constexpr double kKeyMemorySeconds = 30.0;

        // what the detector found in one hop, kept until the delayed audio it describes reaches the output
        struct HopRecord {
            float in_db{kSilenceDb}, key_db{kSilenceDb}, target_db{0.f}, floor_db{0.f};
            HopState state{HopState::kSilent};
        };

        Controls controls_;
        double sample_rate_{48000.0};
        int hop_{240}, max_block_{512};
        int hop_pos_{0};

        std::array<KFilter, kMaxChannels> k_filters_, key_filters_;
        // one per channel: a sum would cancel a voice whose channels are out of polarity
        std::array<VoicingDetector, kMaxChannels> voicing_;
        size_t channels_{1};
        Rider rider_;
        double hop_energy_{0.0}, key_energy_{0.0};
        std::array<double, kLevelHops> energies_{};
        std::array<bool, kLevelHops> voiced_{};
        std::array<double, kKeyHops> key_energies_{};
        size_t level_slot_{0}, key_slot_{0};
        float key_memory_db_{kSilenceDb};
        size_t key_fill_hops_{0};
        std::array<HopRecord, kRecordHops> records_{};
        size_t record_slot_{0};

        // delay line, with a crossfade when the lookahead changes
        std::array<std::vector<float>, kMaxChannels> delay_;
        std::array<std::vector<float>, kMaxChannels> dry_;
        size_t delay_mask_{0}, delay_write_{0};
        int delay_hops_{kLookaheadHops}, old_delay_hops_{kLookaheadHops};
        int fade_pos_{0}, fade_len_{1};

        // gain ramp across each hop
        float gain_{1.f}, gain_step_{0.f};

        std::atomic<float> gain_db_{0.f};
        std::atomic<int> state_{0};
        std::atomic<bool> publishing_{false};
        ui::SpscQueue<Column, kColumns> columns_;

        std::atomic<bool> learning_{false};
        std::atomic<double> learn_energy_{0.0};
        std::atomic<int> learn_hops_{0};

        void endHop();
        float delayed(size_t channel) const;
    };
}
