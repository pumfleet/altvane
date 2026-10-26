// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <mutex>

#include "../common.hpp"
#include "altvane_ui/util/tri_buffer.hpp"
#include "../model/engine_bridge.hpp"
#include "../../dsp/analyzer/fft_analyzer/fft_analyzer_receiver.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_smoother.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_tilter.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_accumulator.hpp"
#include "../../dsp/interpolation/interpolation.hpp"
#include "../../chore/eq_match/eq_match_optimizer.hpp"
#include "../../chore/thread/notifier.hpp"

namespace altvane::eq {
    /**
     * EQ Match: learns the long-term spectrum of the input and of a reference, derives the correction curve,
     * and fits bands to it with the upstream optimizer
     * analysis runs on the spectrum worker thread (it shares the analyzer FIFO), fitting on its own thread,
     * everything else on the message thread
     */
    class MatchEngine final : private juce::Thread, private juce::Timer {
    public:
        enum class Target { kSideChain, kFlat, kNatural, kBalance, kFile };

        enum class State { kOff, kLearning, kFitting, kResult };

        static constexpr size_t kNumPoints = 128;

        struct Frame {
            std::array<float, kNumPoints> freqs{}, source{}, target{}, correction{};
            float seconds{0.f};
            bool valid{false};
        };

        std::function<void()> on_state_changed;

        explicit MatchEngine(EngineBridge& bridge);

        ~MatchEngine() override;

        // ---------------------------------------------------------- spectrum worker thread

        bool isActive() const { return active_.load(std::memory_order::acquire); }

        void prepare(double sample_rate, size_t history_size);

        void feed(zldsp::container::FIFORange range, const std::vector<std::vector<float>>& main_fifo,
                  const std::vector<std::vector<float>>& side_fifo);

        void process();

        // ---------------------------------------------------------- message thread

        void start();

        void relearn();

        void fit();

        /**
         * write the first n fitted bands into the plug-in (others are switched off)
         */
        void applyBands(size_t n);

        /**
         * keep the matched bands as one undo step and leave match mode
         */
        void finish();

        /**
         * restore the bands as they were before matching and leave match mode
         */
        void cancel();

        State getState() const { return state_; }

        size_t getNumApplied() const { return num_applied_; }

        size_t getMaxBands() const { return fitted_.size(); }

        size_t getSuggested() const { return suggested_; }

        bool pullFrame() { return frames_.pull(); }

        const Frame& getFrame() const { return frames_.getReader(); }

        void setTarget(Target target);

        Target getTarget() const { return target_.load(std::memory_order::relaxed); }

        bool loadTargetFile(const juce::File& file);

        bool saveTargetFile(const juce::File& file) const;

        void setAmount(float amount) { amount_.store(amount, std::memory_order::relaxed); }
        float getAmount() const { return amount_.load(std::memory_order::relaxed); }

        void setTilt(float db_per_octave) { tilt_.store(db_per_octave, std::memory_order::relaxed); }
        float getTilt() const { return tilt_.load(std::memory_order::relaxed); }

        void setLimit(float db) { limit_.store(db, std::memory_order::relaxed); }
        float getLimit() const { return limit_.load(std::memory_order::relaxed); }

        /**
         * freehand edits of the correction curve; a value below -100 dB means "not drawn"
         */
        void drawSegment(size_t from_idx, float from_db, size_t to_idx, float to_db);

        void eraseSegment(size_t from_idx, size_t to_idx);

        void clearDrawing();

        bool hasDrawing() const;

    private:
        static constexpr float kNotDrawn = -1000.f;

        EngineBridge& bridge_;
        std::atomic<bool> active_{false};
        State state_{State::kOff};

        // parameters
        std::atomic<Target> target_{Target::kSideChain};
        std::atomic<float> amount_{1.f}, tilt_{0.f}, limit_{12.f};
        std::array<std::atomic<float>, kNumPoints> drawing_{};
        zlchore::thread::Notifier to_reset_{};
        zlchore::thread::Notifier to_update_target_{true};
        mutable std::mutex file_mutex_;
        std::vector<float> file_freqs_, file_dbs_;

        // worker-thread analysis
        double sample_rate_{0.0};
        size_t fft_size_{0};
        zldsp::analyzer::FFTAnalyzerProcessor processor_;
        std::array<zldsp::analyzer::FFTAnalyzerReceiver, 2> receivers_{
            zldsp::analyzer::FFTAnalyzerReceiver{processor_}, zldsp::analyzer::FFTAnalyzerReceiver{processor_}
        };
        std::array<zldsp::analyzer::SpectrumAccumulator, 2> accumulators_;
        zldsp::analyzer::SpectrumSmoother smoother_;
        zldsp::analyzer::SpectrumTilter tilter_;
        zldsp::vector::aligned_vector<float> raw_freqs_, raw_dbs_;
        std::unique_ptr<zldsp::interpolation::SeqMakima<float>> interpolator_;
        std::array<float, kNumPoints> target_db_{};
        double learned_samples_{0.0};
        size_t pending_samples_{0};
        TriBuffer<Frame> frames_;

        // fitting
        std::vector<float> fit_freqs_, fit_correction_;
        double fit_sample_rate_{48000.0};
        std::vector<zldsp::filter::FilterParameters> fitted_;
        size_t suggested_{0}, num_applied_{0};
        bool session_open_{false};
        // guards async callbacks from the fitting thread against this object's destruction
        std::shared_ptr<int> alive_token_ = std::make_shared<int>(0);

        void computeTarget();
        void run() override;
        void timerCallback() override;
        void setState(State state);
        void openSession();
    };
}
