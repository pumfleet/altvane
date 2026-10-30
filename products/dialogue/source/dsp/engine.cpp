// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace altvane::dialogue::dsp {
    namespace {
        // per-hop smoothing of the stem levels (about 20 ms)
        constexpr float kGainSmoothing = .4f;

        // mid and side as halves, so that L = M + S and R = M - S
        void mid(const Spectrum& l, const Spectrum& r, Spectrum& out) {
            for (size_t k = 0; k < kBins; ++k) {
                out[k] = .5f * (l[k] + r[k]);
            }
        }

        void side(const Spectrum& l, const Spectrum& r, Spectrum& out) {
            for (size_t k = 0; k < kBins; ++k) {
                out[k] = .5f * (l[k] - r[k]);
            }
        }

        bool isFinite(const Spectrum& x) {
            bool finite = true;
            for (const auto& v : x) {
                finite = finite && std::isfinite(v.real()) && std::isfinite(v.imag());
            }
            return finite;
        }

        void crossfade(Spectrum& y, const Spectrum& from, const float mix) {
            for (size_t k = 0; k < kBins; ++k) {
                y[k] = from[k] + mix * (y[k] - from[k]);
            }
        }

        /**
         * the transfer of a frame that is all voice, which is what the engine plays when the model has nothing in
         * time: the mix at the voice's level, a voice solo at unity, the other solos silent, Removed the rest
         */
        float fallbackGain(const Listen listen, const float voice_gain) {
            switch (listen) {
                case Listen::kMix: return voice_gain;
                case Listen::kVoice: return 1.f;
                case Listen::kRemoved: return 1.f - voice_gain;
                case Listen::kAmbience:
                case Listen::kReverb: return 0.f;
            }
            return 0.f;
        }
    }

    // ---------------------------------------------------------------- Pipeline

    /**
     * one way of separating: a model and a stereo mode, with the recurrent state of each analysis stream
     * (one for linked or mono, one per channel for stereo, the mid and the side for mid/side)
     */
    class Engine::Pipeline {
    public:
        Pipeline(const std::shared_ptr<SeparatorModel>& model, const StereoMode mode, const int num_channels) :
            mode_(num_channels == 1 ? StereoMode::kLinked : mode),
            num_channels_(num_channels),
            groups_(mode_ == StereoMode::kLinked ? 1 : 2),
            streams_(model, groups_) {
        }

        void reset() {
            streams_.reset();
            for (auto& s : splitters_) {
                s.reset();
            }
            frames_ = 0;
        }

        /**
         * after frames were skipped: the recurrent state carries on (to the model, a splice in its input), but the
         * next kModelLag estimates belong to frames from before the skip, so the pipeline is cold again
         */
        void restart() { frames_ = 0; }

        /**
         * whether the model has seen enough frames for its estimates to belong to frames it was given
         */
        bool isWarm() const { return frames_ > kModelLag; }

        /**
         * computes each stream's transfer, and each stem's at its level
         * @param current each channel's newest frame
         * @param delayed each channel's frame kModelLag hops earlier, which the transfer applies to
         * @return false if an estimate or a transfer is not finite (the state must then be reset)
         */
        bool process(const std::array<const Spectrum*, 2>& current, const std::array<const Spectrum*, 2>& delayed,
                     const std::array<BinWeights, kNumStems>& weights, const std::array<float, kNumStems>& gains,
                     const Listen listen, SeparatorScratch& scratch) {
            ++frames_;
            bool finite = true;
            // what each stream analyses: a channel, or the mid (and the side) of both
            auto in = current;
            auto in_delayed = delayed;
            if (num_channels_ == 2 && mode_ != StereoMode::kStereo) {
                mid(*current[0], *current[1], mid_);
                mid(*delayed[0], *delayed[1], mid_delayed_);
                if (mode_ == StereoMode::kMidSide) {
                    side(*current[0], *current[1], side_);
                    side(*delayed[0], *delayed[1], side_delayed_);
                }
                in = {&mid_, &side_};
                in_delayed = {&mid_delayed_, &side_delayed_};
            }
            // every stream in one run, so two streams read each weight once
            const std::array<Spectrum*, 2> speech{&speech_[0], &speech_[1]};
            const auto groups = static_cast<size_t>(groups_);
            streams_.process(std::span(in.data(), groups), std::span(speech.data(), groups), scratch);
            for (size_t g = 0; g < groups; ++g) {
                auto& t = transfer_[g];
                auto& stems = stem_transfer_[g];
                splitters_[g].split(*in_delayed[g], speech_[g], shares_);
                const auto& weighted = weighted_[g];
                computeTransfer(shares_, weights, gains, listen, t, weighted_[g]);
                for (size_t s = 0; s < kNumStems; ++s) {
                    for (size_t k = 0; k < kBins; ++k) {
                        stems.share[s][k] = gains[s] * weighted.share[s][k];
                    }
                }
                finite = finite && isFinite(speech_[g]) && isFinite(t);
            }
            return finite;
        }

        /**
         * the delayed frames through the transfers of the last process(): the mix, and each stem at its level
         * @param stems if not null, receives each stem, stems[stem][channel]
         */
        void apply(const std::array<const Spectrum*, 2>& x, std::array<Spectrum, 2>& mix,
                   std::array<std::array<Spectrum, 2>, kNumStems>* stems) const {
            decode(x, transfer_[0], transfer_[1], mix);
            if (stems != nullptr) {
                for (size_t s = 0; s < kNumStems; ++s) {
                    decode(x, stem_transfer_[0].share[s], stem_transfer_[1].share[s], (*stems)[s]);
                }
            }
        }

        /**
         * each stem (after sensitivity) of the first channel's delayed frame, for the meters; in mid/side decoded from
         * both streams, L_stem = M·share_mid + S·share_side
         */
        void meterStems(const std::array<const Spectrum*, 2>& x, std::array<Spectrum, kNumStems>& stems) const {
            const auto& l = *x[0];
            const auto& r = *x[1];
            for (size_t s = 0; s < kNumStems; ++s) {
                auto& out = stems[s];
                const auto& share = weighted_[0].share[s];
                if (mode_ == StereoMode::kMidSide) {
                    const auto& share_side = weighted_[1].share[s];
                    for (size_t k = 0; k < kBins; ++k) {
                        out[k] = .5f * (l[k] + r[k]) * share[k] + .5f * (l[k] - r[k]) * share_side[k];
                    }
                } else {
                    for (size_t k = 0; k < kBins; ++k) {
                        out[k] = l[k] * share[k];
                    }
                }
            }
        }

    private:
        StereoMode mode_;
        int num_channels_, groups_;
        SeparatorStreams streams_;
        std::array<StemSplitter, 2> splitters_;
        std::array<Spectrum, 2> transfer_{};
        // per stream, each stem's transfer at its level
        std::array<StemShares, 2> stem_transfer_{};
        Spectrum mid_{}, mid_delayed_{}, side_{}, side_delayed_{};
        std::array<Spectrum, 2> speech_{};
        StemShares shares_{};
        std::array<StemShares, 2> weighted_{};
        int64_t frames_{0};

        /**
         * x through one transfer per stream: each channel times its own (or the linked) transfer, or, in mid/side,
         * L = M·t_mid + S·t_side and R = M·t_mid − S·t_side
         */
        void decode(const std::array<const Spectrum*, 2>& x, const Spectrum& t0, const Spectrum& t1,
                    std::array<Spectrum, 2>& y) const {
            if (mode_ == StereoMode::kMidSide) {
                const auto& l = *x[0];
                const auto& r = *x[1];
                for (size_t k = 0; k < kBins; ++k) {
                    const auto m = .5f * (l[k] + r[k]) * t0[k];
                    const auto s = .5f * (l[k] - r[k]) * t1[k];
                    y[0][k] = m + s;
                    y[1][k] = m - s;
                }
                return;
            }
            for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                const auto& in = *x[c];
                const auto& t = c == 0 || groups_ == 1 ? t0 : t1;
                for (size_t k = 0; k < kBins; ++k) {
                    y[c][k] = in[k] * t[k];
                }
            }
        }
    };

    // ---------------------------------------------------------------- Worker

    class Engine::Worker final : public juce::Thread {
    public:
        explicit Worker(Engine& engine) : Thread("Altvane Dialogue"), engine_(engine) {
            // without permission to run in real time (Linux without rtprio), a high-priority thread rather than none
            if (!startRealtimeThread(RealtimeOptions{}.withPriority(8).withPeriodMs(10.0)
                                                      .withProcessingTimeMs(2.0).withMaximumProcessingTimeMs(8.0))) {
                startThread(Priority::highest);
            }
        }

        ~Worker() override {
            signalThreadShouldExit();
            wake();
            stopThread(4000);
        }

        void wake() {
            sequence_.fetch_add(1, std::memory_order::release);
            sequence_.notify_one();
        }

        void run() override {
            const juce::ScopedNoDenormals no_denormals;
            juce::WorkgroupToken token;
            while (!threadShouldExit()) {
                if (engine_.workgroup_changed_.exchange(false)) {
                    const std::scoped_lock lock(engine_.workgroup_mutex_);
                    token.reset();
                    if (engine_.workgroup_) {
                        engine_.workgroup_.join(token);
                    }
                }
                const auto seen = sequence_.load(std::memory_order::acquire);
                engine_.processAvailable(engine_.realtime_.load(std::memory_order::relaxed), this);
                if (threadShouldExit()) {
                    break;
                }
                sequence_.wait(seen, std::memory_order::acquire);
            }
        }

    private:
        Engine& engine_;
        std::atomic<uint32_t> sequence_{0};
    };

    // ---------------------------------------------------------------- Engine

    Engine::Engine() {
        const auto& window = vorbisWindow();
        for (size_t n = 0; n < kHop; ++n) {
            fade_curve_[n] = window[n] * window[n];
        }
    }

    Engine::~Engine() {
        release();
    }

    bool Engine::prepare(const int num_channels, const int max_block, const int latency_multiple, const bool stems) {
        worker_.reset();
        const std::scoped_lock lock(hop_mutex_);

        num_channels_ = std::clamp(num_channels, 1, 2);
        max_block_ = std::max(max_block, 1);
        stems_ = stems;
        // the algorithm's own delay, then the worker's time: it starts a frame when the block holding the frame's
        // last sample arrives, and the frame is first read budget_blocks blocks later, so it gets kWorkerBudget of
        // wall-clock time and never less than one block (see ARCHITECTURE.md)
        const auto budget_blocks = (std::max(kWorkerBudget, max_block_) + max_block_ - 1) / max_block_;
        latency_ = kAlgorithmicLatency + budget_blocks * max_block_;
        const auto multiple = std::max(latency_multiple, 1);
        latency_ = (latency_ + multiple - 1) / multiple * multiple;

        // in real time the worker reads no input more than two hops older than the oldest output the audio thread may
        // still read (a block and the latency behind what it has written), and the audio thread writes up to a block
        // past that: with room for the latency, two blocks and four frames, no input is read while being overwritten
        // (processAvailable checks it and skips; tests check the margin)
        int64_t ring_size = 1 << 14;
        while (ring_size < latency_ + 2 * max_block_ + 4 * kFftSize) {
            ring_size <<= 1;
        }
        ring_mask_ = ring_size - 1;
        for (size_t c = 0; c < 2; ++c) {
            in_ring_[c].assign(static_cast<size_t>(ring_size), 0.f);
            out_ring_[c].assign(static_cast<size_t>(ring_size), 0.f);
            tail_ring_[c].assign(static_cast<size_t>(ring_size), 0.f);
            for (size_t s = 0; s < kNumStems; ++s) {
                stem_ring_[s][c].assign(stems_ ? static_cast<size_t>(ring_size) : 0, 0.f);
                stem_tail_ring_[s][c].assign(stems_ ? static_cast<size_t>(ring_size) : 0, 0.f);
            }
        }
        written_.store(0);
        produced_.store(0);
        underruns_.store(0);
        skipped_.store(0);
        least_input_margin_.store(std::numeric_limits<int64_t>::max());
        next_frame_ = 0;
        recovering_ = false;
        recovery_pos_ = 0;
        follow_ = 0;
        follow_from_ = 0;
        follow_fade_ = -1;
        committed_ = false;
        for (auto& o : ola_) {
            o.fill(0.f);
        }
        for (auto& stem : stem_ola_) {
            for (auto& o : stem) {
                o.fill(0.f);
            }
        }
        for (auto& channel : history_) {
            for (auto& f : channel) {
                f.fill({});
            }
        }

        if (scratch_ == nullptr) {
            scratch_ = std::make_unique<SeparatorScratch>();
        }
        const std::array models{SeparatorModel::get(Quality::kEco), SeparatorModel::get(Quality::kHigh)};
        for (int q = 0; q < 2; ++q) {
            for (int mode = 0; mode < kNumStereoModes; ++mode) {
                auto& p = pipelines_[static_cast<size_t>(q * kNumStereoModes + mode)];
                const auto& model = models[static_cast<size_t>(q)];
                // a mono input is always linked
                const auto usable = model != nullptr && (mode == 0 || num_channels_ == 2);
                p = usable ? std::make_unique<Pipeline>(model, static_cast<StereoMode>(mode), num_channels_) : nullptr;
            }
        }
        model_ready_.store(models[0] != nullptr || models[1] != nullptr);

        for (size_t s = 0; s < kNumStems; ++s) {
            for (size_t n = 0; n < kSensitivityNodes; ++n) {
                sensitivity_cache_[s][n] = controls_.sensitivity[s][n].load();
            }
            computeSensitivity(sensitivity_cache_[s], weights_[s]);
            gains_[s] = controls_.gains[s].load();
        }
        // the audio thread's fallback gains follow their targets as the worker's levels do, per sample
        fallback_smoothing_ = std::pow(kGainSmoothing, 1.f / static_cast<float>(kHop));
        fallback_voice_gain_ = model_ready_.load() ? gains_[kVoice] : 1.f;
        fallback_gain_ = model_ready_.load()
                             ? fallbackGain(static_cast<Listen>(std::clamp(controls_.listen.load(), 0, 4)), gains_[kVoice])
                             : 1.f;
        active_ = selectPipeline();
        fading_ = -1;
        if (active_ >= 0) {
            pipelines_[static_cast<size_t>(active_)]->reset();
        }

        // analysis bands, log-spaced; a bin counts towards each band by how much of its width the band covers
        band_weights_.clear();
        constexpr auto bin_width = static_cast<float>(kSampleRate) / static_cast<float>(kFftSize);
        for (int b = 0; b < AnalysisFrame::kBands; ++b) {
            const auto ratio = AnalysisFrame::kHighHz / AnalysisFrame::kLowHz;
            const auto lo = AnalysisFrame::kLowHz * std::pow(ratio, static_cast<float>(b) / AnalysisFrame::kBands);
            const auto hi = AnalysisFrame::kLowHz * std::pow(ratio, static_cast<float>(b + 1) / AnalysisFrame::kBands);
            for (int k = 1; k < kBins; ++k) {
                const auto overlap = std::min(hi, (static_cast<float>(k) + .5f) * bin_width)
                                     - std::max(lo, (static_cast<float>(k) - .5f) * bin_width);
                if (overlap > 0.f) {
                    band_weights_.push_back({b, k, overlap / bin_width});
                }
            }
        }

        if (worker_thread_) {
            worker_ = std::make_unique<Worker>(*this);
            {
                const std::scoped_lock wg_lock(workgroup_mutex_);
                workgroup_changed_.store(static_cast<bool>(workgroup_));
            }
            worker_->wake();
        }
        return model_ready_.load();
    }

    void Engine::release() {
        worker_.reset();
    }

    void Engine::setWorkgroup(const juce::AudioWorkgroup& workgroup) {
        const std::scoped_lock lock(workgroup_mutex_);
        workgroup_ = workgroup;
        workgroup_changed_.store(true);
    }

    int Engine::selectPipeline() const {
        const auto quality = std::clamp(controls_.quality.load(std::memory_order::relaxed), 0, 1);
        const auto stereo = std::clamp(controls_.stereo.load(std::memory_order::relaxed), 0, kNumStereoModes - 1);
        const auto mode = num_channels_ == 1 ? 0 : stereo;
        for (const auto q : {quality, 1 - quality}) {
            if (pipelines_[static_cast<size_t>(q * kNumStereoModes + mode)] != nullptr) {
                return q * kNumStereoModes + mode;
            }
        }
        return -1;
    }

    void Engine::process(float* const* channels, const int num_samples, const bool realtime, const StemOutputs* stems) {
        jassert(num_samples <= max_block_);
        const auto start = written_.load(std::memory_order::relaxed);
        for (int c = 0; c < num_channels_; ++c) {
            auto& ring = in_ring_[static_cast<size_t>(c)];
            for (int i = 0; i < num_samples; ++i) {
                // a non-finite sample would stay in the model's running averages and recurrent state for good, so
                // the engine hears it as silence
                const auto x = channels[c][i];
                ring[static_cast<size_t>((start + i) & ring_mask_)] = std::isfinite(x) ? x : 0.f;
            }
        }
        written_.store(start + num_samples, std::memory_order::release);
        realtime_.store(realtime, std::memory_order::relaxed);

        if (realtime) {
            if (worker_ != nullptr) {
                worker_->wake();
            }
        } else {
            processAvailable(false);
        }

        // the output up to produced_; past it, the newest frame's tail completed by the fallback (as if every later
        // frame were all voice, the overlap-add cross-fading into it over a hop), then the fallback. once the output
        // has gone past what was produced, a newer produced_ is cross-faded to over a hop (see ARCHITECTURE.md)
        const auto produced = produced_.load(std::memory_order::acquire);
        if (follow_fade_ < 0) {
            if (!committed_) {
                follow_ = produced;
            } else if (produced > follow_) {
                follow_from_ = follow_;
                follow_ = produced;
                follow_fade_ = 0;
                committed_ = false;
            }
        }
        const auto ready = model_ready_.load(std::memory_order::relaxed);
        const auto voice_target = ready ? controls_.gains[kVoice].load(std::memory_order::relaxed) : 1.f;
        const auto listen = static_cast<Listen>(std::clamp(controls_.listen.load(std::memory_order::relaxed), 0, 4));
        const auto target = ready ? fallbackGain(listen, voice_target) : 1.f;
        const auto keep = std::pow(fallback_smoothing_, static_cast<float>(num_samples));
        const std::array<float, 2> gain{fallback_gain_, target + (fallback_gain_ - target) * keep};
        const std::array<float, 2> voice_gain{fallback_voice_gain_, voice_target + (fallback_voice_gain_ - voice_target) * keep};
        fallback_gain_ = gain[1];
        fallback_voice_gain_ = voice_gain[1];

        const auto first = start - latency_;
        const auto inv_n = 1.f / static_cast<float>(num_samples);
        const auto play = [&](const std::vector<float>& in, const std::vector<float>& out, const std::vector<float>& tail,
                              const std::array<float, 2>& g, float* dest) {
            // the continuation of what was produced up to p, at time t, given the fallback x there
            const auto continuation = [&](const int64_t p, const int64_t t, const float x) {
                const auto slot = static_cast<size_t>(t & ring_mask_);
                if (t < p) {
                    return out[slot];
                }
                // (produced_ is 0 both before and after the first frame, so its tail is not known to be there)
                if (p > 0 && t < p + kHop) {
                    return tail[slot] + x * fade_curve_[static_cast<size_t>(t - p)];
                }
                return x;
            };
            for (int i = 0; i < num_samples; ++i) {
                const auto t = first + i;
                if (t < 0) {
                    dest[i] = 0.f;
                    continue;
                }
                const auto x = (g[0] + (g[1] - g[0]) * static_cast<float>(i + 1) * inv_n) * in[static_cast<size_t>(t & ring_mask_)];
                auto y = continuation(follow_, t, x);
                if (follow_fade_ >= 0 && follow_fade_ + i < kHop) {
                    const auto from = continuation(follow_from_, t, x);
                    y = from + fade_curve_[static_cast<size_t>(follow_fade_ + i)] * (y - from);
                }
                dest[i] = y;
            }
        };
        for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
            play(in_ring_[c], out_ring_[c], tail_ring_[c], gain, channels[c]);
        }
        if (stems != nullptr && stems_) {
            // late, the mix plays the input as voice, so the voice stem does
            constexpr std::array<float, 2> kSilent{};
            for (size_t s = 0; s < kNumStems; ++s) {
                for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                    play(in_ring_[c], stem_ring_[s][c], stem_tail_ring_[s][c], s == kVoice ? voice_gain : kSilent,
                         (*stems)[s][c]);
                }
            }
        }

        const auto end = first + num_samples;
        if (follow_fade_ >= 0) {
            follow_fade_ += num_samples;
            if (follow_fade_ >= kHop) {
                follow_fade_ = -1;
            }
        }
        committed_ = committed_ || end > follow_;
        const auto missing = end - std::max({follow_, first, int64_t{0}});
        if (missing > 0 && ready) {
            underruns_.fetch_add(static_cast<uint64_t>(missing), std::memory_order::relaxed);
        }
    }

    void Engine::runWorkerForTesting() {
        processAvailable(true);
    }

    void Engine::processAvailable(const bool realtime, const juce::Thread* worker) {
        const std::scoped_lock lock(hop_mutex_);
        // frame f covers input [(f - 1) * hop, (f + 1) * hop). a worker asked to stop leaves the rest, so release()
        // never waits for a backlog (the thread would otherwise be killed mid-hop, then outlive the engine)
        while (worker == nullptr || !worker->threadShouldExit()) {
            const auto written = written_.load(std::memory_order::acquire);
            if ((next_frame_ + 1) * kHop > written) {
                break;
            }
            // from now on the audio thread reads no output before `heard` (it may be reading the block just written),
            // so frame f, which completes output up to (f - kModelLag) * hop, can be heard only if that is later.
            // a few inaudible frames are still worth running, since they keep the model's state continuous, but once
            // the next kModelLag + 1 are inaudible, re-warming the model where it can be heard again costs fewer runs
            // than catching up: the worker skips there. and input before intactFrom() may be being overwritten; the
            // ring is sized so that only a frame that far behind can be that old. offline, the audio thread runs every
            // frame before it writes the next block, so it skips nothing (input can only be gone offline if the host
            // switched from real time while the worker was far behind, and then there is nothing exact left to do)
            const auto heard = written - max_block_ - latency_;
            const auto behind = next_frame_ > kModelLag && next_frame_ * kHop <= heard;
            const auto overwritten = (next_frame_ - 1) * kHop < intactFrom(written);
            if ((realtime && behind) || overwritten) {
                skipTo(heard);
                continue;
            }
            processFrame(next_frame_++);
        }
    }

    void Engine::skipTo(const int64_t heard) {
        // delayed frame d completes the output hop [(d - 1) * hop, d * hop), so d's is the first the audio thread
        // may still read, and frame d + kModelLag the first worth running
        const auto d = std::max<int64_t>(heard, 0) / kHop + 1;
        const auto resume = d + kModelLag;
        jassert(resume > next_frame_);
        skipped_.fetch_add(static_cast<uint64_t>(resume - next_frame_), std::memory_order::relaxed);
        updateControls();
        const auto listen = static_cast<Listen>(std::clamp(controls_.listen.load(std::memory_order::relaxed), 0, 4));

        // the frames the next ones apply to, and the one before: the first hop overlaps its second half, which
        // plays as the fallback, as the audio thread has been playing
        for (auto f = d - 1; f < resume; ++f) {
            analyse(f);
        }
        const auto slot = static_cast<size_t>((d - 1) % kHistory);
        const std::array<const Spectrum*, 2> before{&history_[0][slot], &history_[num_channels_ > 1 ? 1 : 0][slot]};
        fallback(before, listen, output_spec_, stems_ ? &stem_spec_ : nullptr);
        for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
            ola_[c].fill(0.f);
            synthesise(output_spec_[c], ola_[c], out_ring_[c], tail_ring_[c], (d - 2) * kHop, false);
            if (stems_) {
                for (size_t s = 0; s < kNumStems; ++s) {
                    stem_ola_[s][c].fill(0.f);
                    synthesise(stem_spec_[s][c], stem_ola_[s][c], stem_ring_[s][c], stem_tail_ring_[s][c],
                               (d - 2) * kHop, false);
                }
            }
        }
        next_frame_ = resume;

        // the model misses the skipped frames: it carries on from where it resumes, and the output stays the
        // fallback until its estimates belong to frames it has seen, then fades in (as on a quality change)
        if (active_ >= 0) {
            fading_ = -1;
            pipelines_[static_cast<size_t>(active_)]->restart();
            recovering_ = true;
            recovery_pos_ = 0;
        }
    }

    void Engine::updateControls() {
        for (size_t s = 0; s < kNumStems; ++s) {
            const auto target = controls_.gains[s].load(std::memory_order::relaxed);
            gains_[s] += (target - gains_[s]) * (1.f - kGainSmoothing);
            if (std::abs(target - gains_[s]) < 1e-6f) {
                gains_[s] = target;
            }
            bool changed = false;
            for (size_t n = 0; n < kSensitivityNodes; ++n) {
                const auto v = controls_.sensitivity[s][n].load(std::memory_order::relaxed);
                changed = changed || v != sensitivity_cache_[s][n];
                sensitivity_cache_[s][n] = v;
            }
            if (changed) {
                computeSensitivity(sensitivity_cache_[s], weights_[s]);
            }
        }
    }

    void Engine::analyse(const int64_t frame) {
        const auto& window = vorbisWindow();
        const auto slot = static_cast<size_t>(frame % kHistory);
        const auto begin = (frame - 1) * kHop;
        for (int c = 0; c < num_channels_; ++c) {
            const auto& ring = in_ring_[static_cast<size_t>(c)];
            for (int n = 0; n < kFftSize; ++n) {
                const auto t = begin + n;
                frame_[static_cast<size_t>(n)] = t < 0 ? 0.f : ring[static_cast<size_t>(t & ring_mask_)] * window[static_cast<size_t>(n)];
            }
            fft_.forward(frame_, history_[static_cast<size_t>(c)][slot]);
        }
        // how far what was just read stayed clear of what the audio thread may have been overwriting meanwhile
        // (never negative: see processAvailable)
        const auto margin = begin - intactFrom(written_.load(std::memory_order::acquire));
        if (margin < least_input_margin_.load(std::memory_order::relaxed)) {
            least_input_margin_.store(margin, std::memory_order::relaxed);
        }
    }

    void Engine::fallback(const std::array<const Spectrum*, 2>& delayed, const Listen listen,
                          std::array<Spectrum, 2>& mix, std::array<std::array<Spectrum, 2>, kNumStems>* stems) {
        const auto gain = fallbackGain(listen, gains_[kVoice]);
        for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
            const auto& x = *delayed[c];
            for (size_t k = 0; k < kBins; ++k) {
                mix[c][k] = gain * x[k];
            }
            if (stems != nullptr) {
                for (size_t k = 0; k < kBins; ++k) {
                    (*stems)[kVoice][c][k] = gains_[kVoice] * x[k];
                }
                (*stems)[kAmbience][c].fill({});
                (*stems)[kReverb][c].fill({});
            }
        }
    }

    void Engine::processFrame(const int64_t frame) {
        updateControls();
        const auto listen = static_cast<Listen>(std::clamp(controls_.listen.load(std::memory_order::relaxed), 0, 4));

        // analyse the newest frame of every channel
        analyse(frame);
        const auto slot = static_cast<size_t>(frame % kHistory);

        // switch quality / stereo mode by running the new pipeline beside the old one until it is warm, then fading
        // (not while recovering from a skip: the pipeline that resumes is cold already)
        if (const auto wanted = selectPipeline(); wanted != active_ && wanted >= 0 && fading_ < 0 && !recovering_) {
            fading_ = active_;
            active_ = wanted;
            pipelines_[static_cast<size_t>(active_)]->reset();
            fade_pos_ = 0;
        }

        const auto delayed_frame = frame - kModelLag;
        const auto delayed_slot = static_cast<size_t>((delayed_frame + kHistory) % kHistory);
        const std::array<const Spectrum*, 2> current{&history_[0][slot], &history_[num_channels_ > 1 ? 1 : 0][slot]};
        const std::array<const Spectrum*, 2> delayed{
            &history_[0][delayed_slot], &history_[num_channels_ > 1 ? 1 : 0][delayed_slot]
        };

        auto* stem_spec = stems_ ? &stem_spec_ : nullptr;
        if (active_ < 0) {
            // nothing to separate with: the input, all of it voice
            for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                output_spec_[c] = *delayed[c];
                stem_spec_[kVoice][c] = *delayed[c];
                stem_spec_[kAmbience][c].fill({});
                stem_spec_[kReverb][c].fill({});
            }
            for (auto& s : meter_stems_) {
                s.fill({});
            }
        } else {
            auto& active = *pipelines_[static_cast<size_t>(active_)];
            auto finite = active.process(current, delayed, weights_, gains_, listen, *scratch_);
            active.apply(delayed, output_spec_, stem_spec);
            // the meters follow the old pipeline until the fade completes
            const Pipeline* metered = &active;
            if (fading_ >= 0) {
                auto& old = *pipelines_[static_cast<size_t>(fading_)];
                finite = old.process(current, delayed, weights_, gains_, listen, *scratch_) && finite;
                old.apply(delayed, output_fading_, stems_ ? &stem_spec_fading_ : nullptr);
                const auto mix = active.isWarm() ? static_cast<float>(++fade_pos_) / static_cast<float>(kFadeFrames) : 0.f;
                // the outputs fade, not the transfers: the two pipelines may not work on the same signals (L/R, M/S)
                for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                    crossfade(output_spec_[c], output_fading_[c], mix);
                    if (stems_) {
                        for (size_t s = 0; s < kNumStems; ++s) {
                            crossfade(stem_spec_[s][c], stem_spec_fading_[s][c], mix);
                        }
                    }
                }
                if (mix < 1.f) {
                    metered = &old;
                } else {
                    fading_ = -1;
                }
            }
            if (!finite) {
                // extreme input (the audio thread already replaces non-finite samples) overflowed the model or the
                // splitter: what is not finite would stay in their recurrent state, so they start afresh
                active.reset();
                if (fading_ >= 0) {
                    pipelines_[static_cast<size_t>(fading_)]->reset();
                    fading_ = -1;
                }
                recovering_ = true;
                recovery_pos_ = 0;
            }
            if (recovering_) {
                // the fallback until the model is warm again, then a fade to its output
                fallback(delayed, listen, output_fading_, stems_ ? &stem_spec_fading_ : nullptr);
                const auto warm = active.isWarm();
                const auto mix = warm ? static_cast<float>(++recovery_pos_) / static_cast<float>(kFadeFrames) : 0.f;
                for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                    crossfade(output_spec_[c], output_fading_[c], mix);
                    if (stems_) {
                        for (size_t s = 0; s < kNumStems; ++s) {
                            crossfade(stem_spec_[s][c], stem_spec_fading_[s][c], mix);
                        }
                    }
                }
                recovering_ = recovery_pos_ < kFadeFrames;
                if (!warm) {
                    metered = nullptr;
                }
            }
            if (metered != nullptr) {
                metered->meterStems(delayed, meter_stems_);
            } else {
                // as the fallback plays it: all voice
                meter_stems_[kVoice] = *delayed[0];
                meter_stems_[kAmbience].fill({});
                meter_stems_[kReverb].fill({});
            }
        }

        if (delayed_frame < 0) {
            return;
        }

        // synthesise and overlap-add: frame (frame - lag) completes output [(frame - lag - 1) * hop, (frame - lag) * hop)
        const auto out_begin = (delayed_frame - 1) * kHop;
        for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
            synthesise(output_spec_[c], ola_[c], out_ring_[c], tail_ring_[c], out_begin);
            if (stems_) {
                for (size_t s = 0; s < kNumStems; ++s) {
                    synthesise(stem_spec_[s][c], stem_ola_[s][c], stem_ring_[s][c], stem_tail_ring_[s][c], out_begin);
                }
            }
        }
        produced_.store(std::max<int64_t>(out_begin + kHop, 0), std::memory_order::release);

        publishAnalysis(*delayed[0], meter_stems_, output_spec_[0]);
    }

    void Engine::synthesise(const Spectrum& y, std::array<float, kFftSize>& ola, std::vector<float>& ring,
                            std::vector<float>& tail, const int64_t out_begin, const bool write) {
        const auto& window = vorbisWindow();
        fft_.inverse(y, synth_);
        for (size_t n = 0; n < kFftSize; ++n) {
            ola[n] += synth_[n] * window[n];
        }
        if (write) {
            for (int n = 0; n < kHop; ++n) {
                const auto time = out_begin + n;
                if (time >= 0) {
                    ring[static_cast<size_t>(time & ring_mask_)] = ola[static_cast<size_t>(n)];
                }
            }
        }
        std::copy(ola.begin() + kHop, ola.end(), ola.begin());
        std::fill(ola.begin() + kFftSize - kHop, ola.end(), 0.f);
        // what this frame leaves in the next hop, which the audio thread completes if the next frame is late
        if (write) {
            for (int n = 0; n < kHop; ++n) {
                const auto time = out_begin + kHop + n;
                if (time >= 0) {
                    tail[static_cast<size_t>(time & ring_mask_)] = ola[static_cast<size_t>(n)];
                }
            }
        }
    }

    void Engine::publishAnalysis(const Spectrum& input, const std::array<Spectrum, kNumStems>& stems,
                                 const Spectrum& output) {
        auto& frame = analysis_.getWriter();
        frame.input.fill(0.f);
        frame.output.fill(0.f);
        for (auto& s : frame.stems) {
            s.fill(0.f);
        }
        constexpr float kNorm = 4.f / (static_cast<float>(kFftSize) * static_cast<float>(kFftSize));
        for (const auto& w : band_weights_) {
            const auto band = static_cast<size_t>(w.band);
            const auto k = static_cast<size_t>(w.bin);
            const auto weight = kNorm * w.weight;
            frame.input[band] += std::norm(input[k]) * weight;
            frame.output[band] += std::norm(output[k]) * weight;
            for (size_t s = 0; s < kNumStems; ++s) {
                frame.stems[s][band] += std::norm(stems[s][k]) * weight;
            }
        }
        frame.index = ++analysis_index_;
        analysis_.publish();
    }

    bool Engine::pullAnalysis(AnalysisFrame& frame) {
        if (!analysis_.pull()) {
            return false;
        }
        frame = analysis_.getReader();
        return true;
    }
}
