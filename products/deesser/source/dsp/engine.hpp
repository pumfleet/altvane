// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <memory>

#include "detector.hpp"
#include "feeds.hpp"
#include "gain_computer.hpp"

namespace altvane::deesser::dsp {
    enum class Processing { kWide, kSplit };

    enum class Shape { kShelf, kBell };

    enum class StereoMode { kStereo, kMid, kSide };

    /**
     * what the output plays: the de-essed signal, the detection band (Trigger) or what is taken away (Delta)
     */
    enum class Listen { kOff, kTrigger, kDelta };

    struct Controls {
        float threshold_db{-24.f}, range_db{9.f};
        Mode mode{Mode::kVocal};
        Processing processing{Processing::kSplit};
        Shape shape{Shape::kShelf};
        float band_low{5500.f}, band_high{20000.f};
        bool band_open{true};
        float lookahead_ms{5.f};
        float link{1.f};
        StereoMode stereo{StereoMode::kStereo};
        float output_db{0.f};
        bool bypass{false};
        Listen listen{Listen::kOff};
    };

    /**
     * the de-esser: per channel, the detection band's level against the threshold sets a gain reduction, which
     * turns down the delayed audio, either all of it (Wide) or only above the band's low edge (Split, Shelf) or
     * only the band (Split, Bell).
     * the reduction is mixed in (x + (g - 1) * part), so with nothing to reduce the output is exactly the input,
     * delayed by the look-ahead; shapes, listen modes and bypass crossfade, and look-ahead changes fade between
     * delay taps. everything runs on a fixed 32-sample control grid, so the result never depends on block size
     */
    class Engine {
    public:
        static constexpr float kMaxLookaheadMs = 15.f;

        Engine();

        void setControls(const Controls& c) { controls_ = c; }

        const Controls& getControls() const { return controls_; }

        /**
         * allocates; snaps every smoothed control to the current controls
         */
        void prepare(double sample_rate, int max_channels);

        void reset();

        /**
         * @param io the main channels (one or two), processed in place
         * @param side the detection source (an external side chain), or nullptr for the main input
         */
        void process(float* const* io, int channels, const float* const* side, int side_channels, int n);

        static int lookaheadSamples(float ms, double sample_rate);

        double getSampleRate() const { return sample_rate_; }

        // ---- feeds for the editor (the audio thread writes only while active)

        void setFeedsActive(const bool active) { feeds_active_.store(active, std::memory_order::relaxed); }

        bool pullHistory(HistoryFrame& frame) { return history_->pop(frame); }

        bool pullAnalysis(AnalysisSample& sample) { return analysis_->pop(sample); }

        /**
         * the peak level (dB) of each ess heard, for the threshold learner
         */
        bool pullEvent(float& peak_db) { return events_->pop(peak_db); }

        void clearFeeds();

        /**
         * the largest gain reduction (dB) since the last call
         */
        float takeReductionPeak() { return reduction_peak_.exchange(0.f, std::memory_order::relaxed); }

    private:
        static constexpr int kControlInterval = 32;

        class DelayLine {
        public:
            void prepare(int max_delay);

            void push(const float x) {
                buffer_[write_ & mask_] = x;
                ++write_;
            }

            float read(const int delay) const {
                return buffer_[(write_ - 1 - static_cast<size_t>(delay)) & mask_];
            }

            void reset();

        private:
            std::vector<float> buffer_{0.f};
            size_t mask_{0}, write_{0};
        };

        Controls controls_;
        double sample_rate_{48000.0};

        std::array<Detector, 2> detectors_;
        std::array<GainComputer, 2> computers_;
        std::array<SVF, 2> shelf_, bell_;
        std::array<DelayLine, 2> audio_delay_, band_delay_;
        Detector::Filters detection_;
        SVFCoeffs shelf_coeffs_, bell_coeffs_;
        float shelf_g_{.4f};

        int control_count_{0};
        float low_log_{0.f}, high_log_{0.f}, freq_smooth_{1.f};
        Ramp wide_, shelf_weight_, bell_weight_, normal_, trigger_, delta_, bypass_, output_;
        int lookahead_{0}, next_lookahead_{0}, fade_pos_{0}, fade_length_{1};
        bool fading_{false};
        // the stereo mode last block (-2 for mono, -1 before the first), to notice a change
        int layout_{-1};

        // feeds
        std::atomic<bool> feeds_active_{false};
        std::unique_ptr<SpscRing<HistoryFrame, kHistoryCapacity>> history_;
        std::unique_ptr<SpscRing<AnalysisSample, kAnalysisCapacity>> analysis_;
        std::unique_ptr<SpscRing<float, kEventCapacity>> events_;
        int history_count_{0}, history_interval_{200};
        HistoryFrame history_peak_;
        float history_in_{0.f}, history_out_{0.f};
        std::atomic<float> reduction_peak_{0.f};

        // ess events (for the learner): a run of sibilance within 30 dB of the loudest band level lately
        float band_hold_{0.f}, hold_decay_{1.f};
        bool in_event_{false};
        float event_peak_{0.f};
        // the band level below which nothing is reduced (the knee's foot), linear, per control period
        float knee_foot_{0.f};
        float history_band_{0.f};
        int event_length_{0}, event_quiet_{0}, event_min_length_{1}, event_gap_{1};

        void updateControlPeriod(bool snap);
        float readDelayed(const DelayLine& line) const;
        void trackEvent(float level, bool sibilant);
    };
}
