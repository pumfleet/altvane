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
#include <cmath>
#include <mutex>
#include <juce_audio_basics/juce_audio_basics.h>
#include <altvane_ui/util/tri_buffer.hpp>

#include "separator.hpp"
#include "stems.hpp"

namespace altvane::dialogue::dsp {
    /**
     * how a stereo input is separated: one analysis of the mid applied to both channels, one per channel,
     * or one of the mid and one of the side; a mono input is always linked
     */
    enum class StereoMode { kLinked, kStereo, kMidSide };

    inline constexpr int kNumStereoModes = 3;

    /**
     * the controls the engine reads, written by the processor from its parameters
     */
    struct EngineControls {
        std::array<std::atomic<float>, kNumStems> gains{}; // linear
        std::array<std::array<std::atomic<float>, kSensitivityNodes>, kNumStems> sensitivity{}; // -1 .. 1
        std::atomic<int> listen{0}; // Listen
        std::atomic<int> quality{0}; // Quality
        std::atomic<int> stereo{0}; // StereoMode

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
     * where process() writes each stem at its level, stems[stem][channel]
     */
    using StemOutputs = std::array<std::array<float*, 2>, kNumStems>;

    /**
     * what the editor draws: band energies (power) of the input and of each stem, ~100 times a second
     */
    struct AnalysisFrame {
        static constexpr int kBands = 64;
        static constexpr float kLowHz = 30.f;
        static constexpr float kHighHz = 20000.f;

        static float bandCentre(const int band) {
            return kLowHz * std::pow(kHighHz / kLowHz, (static_cast<float>(band) + .5f) / static_cast<float>(kBands));
        }

        std::array<float, kBands> input{}, output{};
        std::array<std::array<float, kBands>, kNumStems> stems{};
        uint64_t index{0};
    };

    /**
     * dialogue separation at 48 kHz, for one or two channels
     *
     * the audio thread only copies samples in and out of two rings indexed by absolute sample time;
     * a worker thread analyses each hop, runs the model, applies the stem levels and overlap-adds the result.
     * the output is the input processed and delayed by getLatency(); where the worker has not delivered in time,
     * the output cross-fades to the delayed input as if it were all voice (at the voice's level), so it never drops
     * out, and back once the worker catches up. a worker that has fallen behind skips the frames nobody will hear
     * and re-warms the model before fading back in; offline, the caller waits for every frame
     *
     * with stems, each stem at its level is synthesised too, and the stems add up to the mix (Listen::kMix);
     * a late sample, and a frame the model could not separate, count as voice
     */
    class Engine {
    public:
        Engine();

        ~Engine();

        /**
         * allocates and loads the models; stops the worker while it runs
         * @param max_block the largest number of samples a process() call will get
         * @param latency_multiple the latency is rounded up to a multiple of this (resampling alignment)
         * @param stems whether process() will be asked for the stems as well as the mix
         * @return false if no model could be loaded (the engine then passes the input through, delayed)
         */
        bool prepare(int num_channels, int max_block, int latency_multiple = 1, bool stems = false);

        void release();

        int getLatency() const { return latency_; }

        int getNumChannels() const { return num_channels_; }

        bool hasStems() const { return stems_; }

        /**
         * process in place; in real time the model never blocks the caller, offline the caller waits for it
         * @param stems where to write each stem, if the engine was prepared with stems
         */
        void process(float* const* channels, int num_samples, bool realtime, const StemOutputs* stems = nullptr);

        EngineControls& getControls() { return controls_; }

        /**
         * the latest analysis, if there is a new one (message thread)
         */
        bool pullAnalysis(AnalysisFrame& frame);

        /**
         * samples replaced by the delayed input because the model was late
         */
        uint64_t getUnderruns() const { return underruns_.load(std::memory_order::relaxed); }

        /**
         * frames the worker skipped because it had fallen too far behind to deliver them in time
         */
        uint64_t getSkippedFrames() const { return skipped_.load(std::memory_order::relaxed); }

        bool isModelReady() const { return model_ready_.load(std::memory_order::relaxed); }

        /**
         * the host's audio workgroup, which the worker joins so it runs alongside the audio thread
         */
        void setWorkgroup(const juce::AudioWorkgroup& workgroup);

        /**
         * tests: whether prepare() starts the worker thread; without it, real-time frames are only processed by
         * runWorkerForTesting(), so a test decides exactly when the worker runs and when it stalls
         */
        void setWorkerThreadForTesting(bool enabled) { worker_thread_ = enabled; }

        /**
         * tests: what the worker thread would do now, on the calling thread (skipping frames it is too late for)
         */
        void runWorkerForTesting();

        /**
         * tests: the least margin, in samples, by which the input the worker has read since prepare() was clear of
         * what the audio thread may overwrite (negative if it read input the audio thread could be overwriting)
         */
        int64_t getLeastInputMarginForTesting() const { return least_input_margin_.load(std::memory_order::relaxed); }

    private:
        class Pipeline;
        class Worker;

        static constexpr int kHistory = 8; // analysed frames kept, more than kModelLag
        static constexpr int kFadeFrames = 8;
        // the least wall-clock time the worker gets for a frame (in samples): one hop, 10 ms, which also covers a
        // quality or stereo-mode change running two pipelines at once
        static constexpr int kWorkerBudget = kHop;

        EngineControls controls_;
        int num_channels_{0}, max_block_{0}, latency_{0};
        bool stems_{false};
        int64_t ring_mask_{0};
        // the output up to produced_, and past it the newest frame's overlap-add tail (one hop, the frame's second
        // half), which the audio thread completes with the fallback if the next frame is late
        std::array<std::vector<float>, 2> in_ring_, out_ring_, tail_ring_;
        std::array<std::array<std::vector<float>, 2>, kNumStems> stem_ring_, stem_tail_ring_;
        alignas(64) std::atomic<int64_t> written_{0};
        alignas(64) std::atomic<int64_t> produced_{0};
        alignas(64) std::atomic<uint64_t> underruns_{0};
        std::atomic<uint64_t> skipped_{0};
        std::atomic<int64_t> least_input_margin_{0};
        std::atomic<bool> model_ready_{false};
        // whether the host last called process() in real time, so the worker may skip frames
        std::atomic<bool> realtime_{false};
        // the squared Vorbis window's rising half: the overlap-add's own cross-fade over a hop
        std::array<float, kHop> fade_curve_{};

        // audio-thread state: the output follows the continuation of `follow_` (the output ring before it, then
        // its tail and the fallback); `committed_` once it has played past `follow_`, after which a newer
        // produced_ is faded to from the continuation of `follow_from_` over a hop (follow_fade_ < 0: no fade)
        int64_t follow_{0}, follow_from_{0};
        int follow_fade_{-1};
        bool committed_{false};
        // the fallback's gains, smoothed like the worker's levels: the mix, and the voice stem
        float fallback_gain_{1.f}, fallback_voice_gain_{1.f}, fallback_smoothing_{0.f};

        // worker state, guarded by hop_mutex_ (never taken by the real-time audio thread)
        std::mutex hop_mutex_;
        int64_t next_frame_{0};
        // after a skip (or a model reset), the output is the fallback until the model is warm again, then fades in
        bool recovering_{false};
        int recovery_pos_{0};
        Fft fft_;
        std::array<float, kFftSize> frame_{};
        std::array<std::array<Spectrum, kHistory>, 2> history_{};
        std::array<std::array<float, kFftSize>, 2> ola_{};
        std::array<std::array<std::array<float, kFftSize>, 2>, kNumStems> stem_ola_{};
        // one per quality and stereo mode: quality * kNumStereoModes + mode
        std::array<std::unique_ptr<Pipeline>, 2 * kNumStereoModes> pipelines_;
        std::unique_ptr<SeparatorScratch> scratch_;
        int active_{-1}, fading_{-1}, fade_pos_{0};
        std::array<std::array<float, kSensitivityNodes>, kNumStems> sensitivity_cache_{};
        std::array<BinWeights, kNumStems> weights_{};
        std::array<float, kNumStems> gains_{1.f, 1.f, 1.f};
        // per channel, the mix and each stem at its level (stem_spec_[stem][channel]), of the active and fading pipelines
        std::array<Spectrum, 2> output_spec_{}, output_fading_{};
        std::array<std::array<Spectrum, 2>, kNumStems> stem_spec_{}, stem_spec_fading_{};
        std::array<Spectrum, kNumStems> meter_stems_{}; // the first channel's stems, for the analysis
        std::array<float, kFftSize> synth_{};
        ui::TriBuffer<AnalysisFrame> analysis_;
        uint64_t analysis_index_{0};
        // each bin's share of each analysis band it overlaps
        struct BandWeight {
            int band, bin;
            float weight;
        };
        std::vector<BandWeight> band_weights_;

        std::unique_ptr<Worker> worker_;
        bool worker_thread_{true};
        std::mutex workgroup_mutex_;
        juce::AudioWorkgroup workgroup_;
        std::atomic<bool> workgroup_changed_{false};

        /**
         * run every hop whose input has arrived
         * @param realtime whether frames nobody will hear any more are skipped (never offline)
         * @param worker the worker thread, which stops early when it should exit; nullptr runs them all
         */
        void processAvailable(bool realtime, const juce::Thread* worker = nullptr);
        /**
         * skip to the first output hop the audio thread may still read
         * @param heard the audio thread reads no output before this from now on
         */
        void skipTo(int64_t heard);
        void analyse(int64_t frame);
        void processFrame(int64_t frame);
        /**
         * the input as if it were all voice, at the levels: what plays when the model has nothing in time
         */
        void fallback(const std::array<const Spectrum*, 2>& delayed, Listen listen, std::array<Spectrum, 2>& mix,
                      std::array<std::array<Spectrum, 2>, kNumStems>* stems);
        /**
         * @param write false to only overlap-add, leaving the rings alone
         */
        void synthesise(const Spectrum& y, std::array<float, kFftSize>& ola, std::vector<float>& ring,
                        std::vector<float>& tail, int64_t out_begin, bool write = true);
        /**
         * the first input sample the audio thread cannot be overwriting, given what it has written
         */
        int64_t intactFrom(int64_t written) const { return written + max_block_ - (ring_mask_ + 1); }
        int selectPipeline() const;
        void updateControls();
        void publishAnalysis(const Spectrum& input, const std::array<Spectrum, kNumStems>& stems,
                             const Spectrum& output);
    };
}
