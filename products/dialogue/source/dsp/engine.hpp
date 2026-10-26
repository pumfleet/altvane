// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <atomic>
#include <mutex>
#include <juce_audio_basics/juce_audio_basics.h>
#include <altvane_ui/util/tri_buffer.hpp>

#include "separator.hpp"
#include "stems.hpp"

namespace altvane::dialogue::dsp {
    /**
     * the controls the engine reads, written by the processor from its parameters
     */
    struct EngineControls {
        std::array<std::atomic<float>, kNumStems> gains{}; // linear
        std::array<std::array<std::atomic<float>, kSensitivityNodes>, kNumStems> sensitivity{}; // -1 .. 1
        std::atomic<int> listen{0}; // Listen
        std::atomic<int> quality{0}; // Quality
        std::atomic<bool> linked{true}; // stereo: one analysis of the mid, or one per channel

        EngineControls() {
            for (auto& g : gains) {
                g.store(1.f);
            }
            for (auto& stem : sensitivity) {
                for (auto& node : stem) {
                    node.store(0.f);
                }
            }
        }
    };

    /**
     * what the editor draws: band energies (power) of the input and of each stem, ~100 times a second
     */
    struct AnalysisFrame {
        static constexpr int kBands = 64;
        static constexpr float kLowHz = 30.f;
        static constexpr float kHighHz = 20000.f;

        std::array<float, kBands> input{}, output{};
        std::array<std::array<float, kBands>, kNumStems> stems{};
        uint64_t index{0};
    };

    /**
     * dialogue separation at 48 kHz, for one or two channels
     *
     * the audio thread only copies samples in and out of two rings indexed by absolute sample time;
     * a worker thread analyses each hop, runs the model, applies the stem levels and overlap-adds the result.
     * the output is the input processed and delayed by getLatency(); a sample the worker has not delivered in time
     * is replaced by the delayed input, so the output never drops out
     */
    class Engine {
    public:
        Engine();

        ~Engine();

        /**
         * allocates and loads the models; stops the worker while it runs
         * @param max_block the largest number of samples a process() call will get
         * @param latency_multiple the latency is rounded up to a multiple of this (resampling alignment)
         * @return false if no model could be loaded (the engine then passes the input through, delayed)
         */
        bool prepare(int num_channels, int max_block, int latency_multiple = 1);

        void release();

        int getLatency() const { return latency_; }

        int getNumChannels() const { return num_channels_; }

        /**
         * process in place; in real time the model never blocks the caller, offline the caller waits for it
         */
        void process(float* const* channels, int num_samples, bool realtime);

        EngineControls& getControls() { return controls_; }

        /**
         * the latest analysis, if there is a new one (message thread)
         */
        bool pullAnalysis(AnalysisFrame& frame);

        /**
         * samples replaced by the delayed input because the model was late
         */
        uint64_t getUnderruns() const { return underruns_.load(std::memory_order::relaxed); }

        bool isModelReady() const { return model_ready_.load(std::memory_order::relaxed); }

        /**
         * the host's audio workgroup, which the worker joins so it runs alongside the audio thread
         */
        void setWorkgroup(const juce::AudioWorkgroup& workgroup);

        /**
         * run every available hop on the calling thread (tests)
         */
        void processPendingForTesting();

    private:
        class Pipeline;
        class Worker;

        static constexpr int kHistory = 8; // analysed frames kept, more than kModelLag
        static constexpr int kFadeFrames = 8;

        EngineControls controls_;
        int num_channels_{0}, max_block_{0}, latency_{0};
        int64_t ring_mask_{0};
        std::array<std::vector<float>, 2> in_ring_, out_ring_;
        alignas(64) std::atomic<int64_t> written_{0};
        alignas(64) std::atomic<int64_t> produced_{0};
        alignas(64) std::atomic<uint64_t> underruns_{0};
        std::atomic<bool> model_ready_{false};

        // worker state, guarded by hop_mutex_ (never taken by the real-time audio thread)
        std::mutex hop_mutex_;
        int64_t next_frame_{0};
        Fft fft_;
        std::array<float, kFftSize> frame_{};
        std::array<std::array<Spectrum, kHistory>, 2> history_{};
        std::array<std::array<float, kFftSize>, 2> ola_{};
        std::array<std::unique_ptr<Pipeline>, 4> pipelines_;
        int active_{-1}, fading_{-1}, fade_pos_{0};
        std::array<std::array<float, kSensitivityNodes>, kNumStems> sensitivity_cache_{};
        std::array<BinWeights, kNumStems> weights_{};
        std::array<float, kNumStems> gains_{1.f, 1.f, 1.f};
        std::array<Spectrum, 2> transfer_{}, transfer_fading_{};
        StemShares meter_shares_{}, meter_shares_fading_{};
        std::array<Spectrum, 2> output_spec_{};
        std::array<float, kFftSize> synth_{};
        ui::TriBuffer<AnalysisFrame> analysis_;
        uint64_t analysis_index_{0};
        std::array<int, kBins> band_of_bin_{};

        std::unique_ptr<Worker> worker_;
        std::mutex workgroup_mutex_;
        juce::AudioWorkgroup workgroup_;
        std::atomic<bool> workgroup_changed_{false};

        void processAvailable();
        void processFrame(int64_t frame);
        int selectPipeline() const;
        void updateControls();
        void publishAnalysis(const Spectrum& input, const StemShares& shares, const Spectrum& transfer);
    };
}
