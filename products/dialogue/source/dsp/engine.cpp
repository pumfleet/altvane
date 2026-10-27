// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "engine.hpp"

#include <cmath>

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
            groups_(mode_ == StereoMode::kLinked ? 1 : 2) {
            for (int g = 0; g < groups_; ++g) {
                streams_.emplace_back(model);
            }
        }

        void reset() {
            for (auto& s : streams_) {
                s.reset();
            }
            for (auto& s : splitters_) {
                s.reset();
            }
            frames_ = 0;
        }

        /**
         * whether the model has seen enough frames for its estimates to belong to frames it was given
         */
        bool isWarm() const { return frames_ > kModelLag; }

        /**
         * computes each stream's transfer
         * @param current each channel's newest frame
         * @param delayed each channel's frame kModelLag hops earlier, which the transfer applies to
         */
        void process(const std::array<const Spectrum*, 2>& current, const std::array<const Spectrum*, 2>& delayed,
                     const std::array<BinWeights, kNumStems>& weights, const std::array<float, kNumStems>& gains,
                     const Listen listen, StemShares& meter_shares) {
            ++frames_;
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
            for (size_t g = 0; g < static_cast<size_t>(groups_); ++g) {
                auto& t = transfer_[g];
                if (!streams_[g].process(*in[g], speech_)) {
                    // the model failed: pass this frame through
                    t.fill(Complex{1.f, 0.f});
                    continue;
                }
                splitters_[g].split(*in_delayed[g], speech_, shares_);
                computeTransfer(shares_, weights, gains, listen, t, g == 0 ? meter_shares : weighted_);
            }
        }

        /**
         * the delayed frames through the transfers of the last process(): each channel times its own (or the
         * linked) transfer, or, in mid/side, L = M·T_mid + S·T_side and R = M·T_mid − S·T_side
         */
        void apply(const std::array<const Spectrum*, 2>& x, std::array<Spectrum, 2>& y) const {
            if (mode_ == StereoMode::kMidSide) {
                const auto& l = *x[0];
                const auto& r = *x[1];
                for (size_t k = 0; k < kBins; ++k) {
                    const auto m = .5f * (l[k] + r[k]) * transfer_[0][k];
                    const auto s = .5f * (l[k] - r[k]) * transfer_[1][k];
                    y[0][k] = m + s;
                    y[1][k] = m - s;
                }
                return;
            }
            for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                const auto& in = *x[c];
                const auto& t = transfer_[groups_ == 1 ? 0 : c];
                for (size_t k = 0; k < kBins; ++k) {
                    y[c][k] = in[k] * t[k];
                }
            }
        }

    private:
        StereoMode mode_;
        int num_channels_, groups_;
        std::vector<SeparatorStream> streams_;
        std::array<StemSplitter, 2> splitters_;
        std::array<Spectrum, 2> transfer_{};
        Spectrum mid_{}, mid_delayed_{}, side_{}, side_delayed_{}, speech_{};
        StemShares shares_{}, weighted_{};
        int64_t frames_{0};
    };

    // ---------------------------------------------------------------- Worker

    class Engine::Worker final : public juce::Thread {
    public:
        explicit Worker(Engine& engine) : Thread("Altvane Dialogue"), engine_(engine) {
            startRealtimeThread(RealtimeOptions{}.withPriority(8).withPeriodMs(10.0)
                                                 .withProcessingTimeMs(2.0).withMaximumProcessingTimeMs(8.0));
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
                engine_.processAvailable();
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

    Engine::Engine() = default;

    Engine::~Engine() {
        release();
    }

    bool Engine::prepare(const int num_channels, const int max_block, const int latency_multiple) {
        worker_.reset();
        const std::scoped_lock lock(hop_mutex_);

        num_channels_ = std::clamp(num_channels, 1, 2);
        max_block_ = std::max(max_block, 1);
        // the algorithm's own delay, one hop for the worker to compute in, and a block for it to arrive in
        latency_ = kAlgorithmicLatency + kHop + max_block_;
        const auto multiple = std::max(latency_multiple, 1);
        latency_ = (latency_ + multiple - 1) / multiple * multiple;

        int64_t ring_size = 1 << 14;
        while (ring_size < latency_ + max_block_ + 4 * kFftSize) {
            ring_size <<= 1;
        }
        ring_mask_ = ring_size - 1;
        for (int c = 0; c < 2; ++c) {
            in_ring_[static_cast<size_t>(c)].assign(static_cast<size_t>(ring_size), 0.f);
            out_ring_[static_cast<size_t>(c)].assign(static_cast<size_t>(ring_size), 0.f);
        }
        written_.store(0);
        produced_.store(0);
        underruns_.store(0);
        next_frame_ = 0;
        for (auto& o : ola_) {
            o.fill(0.f);
        }
        for (auto& channel : history_) {
            for (auto& f : channel) {
                f.fill({});
            }
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

        worker_ = std::make_unique<Worker>(*this);
        {
            const std::scoped_lock wg_lock(workgroup_mutex_);
            workgroup_changed_.store(static_cast<bool>(workgroup_));
        }
        worker_->wake();
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

    void Engine::process(float* const* channels, const int num_samples, const bool realtime) {
        jassert(num_samples <= max_block_);
        const auto start = written_.load(std::memory_order::relaxed);
        for (int c = 0; c < num_channels_; ++c) {
            auto& ring = in_ring_[static_cast<size_t>(c)];
            for (int i = 0; i < num_samples; ++i) {
                ring[static_cast<size_t>((start + i) & ring_mask_)] = channels[c][i];
            }
        }
        written_.store(start + num_samples, std::memory_order::release);

        if (realtime) {
            if (worker_ != nullptr) {
                worker_->wake();
            }
        } else {
            processAvailable();
        }

        const auto produced = produced_.load(std::memory_order::acquire);
        const auto first = start - latency_;
        int missing = 0;
        for (int c = 0; c < num_channels_; ++c) {
            const auto& in = in_ring_[static_cast<size_t>(c)];
            const auto& out = out_ring_[static_cast<size_t>(c)];
            auto* dest = channels[c];
            for (int i = 0; i < num_samples; ++i) {
                const auto t = first + i;
                if (t < 0) {
                    dest[i] = 0.f;
                } else if (t < produced) {
                    dest[i] = out[static_cast<size_t>(t & ring_mask_)];
                } else {
                    // late: the delayed input, so the output never drops out
                    dest[i] = in[static_cast<size_t>(t & ring_mask_)];
                    missing += c == 0 ? 1 : 0;
                }
            }
        }
        if (missing > 0 && model_ready_.load(std::memory_order::relaxed)) {
            underruns_.fetch_add(static_cast<uint64_t>(missing), std::memory_order::relaxed);
        }
    }

    void Engine::processPendingForTesting() {
        processAvailable();
    }

    void Engine::processAvailable() {
        const std::scoped_lock lock(hop_mutex_);
        // frame f covers input [(f - 1) * hop, (f + 1) * hop)
        while ((next_frame_ + 1) * kHop <= written_.load(std::memory_order::acquire)) {
            processFrame(next_frame_++);
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

    void Engine::processFrame(const int64_t frame) {
        updateControls();
        const auto listen = static_cast<Listen>(std::clamp(controls_.listen.load(std::memory_order::relaxed), 0, 4));
        const auto& window = vorbisWindow();

        // analyse the newest frame of every channel
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

        // switch quality / stereo mode by running the new pipeline beside the old one until it is warm, then fading
        if (const auto wanted = selectPipeline(); wanted != active_ && wanted >= 0 && fading_ < 0) {
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

        if (active_ < 0) {
            for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                output_spec_[c] = *delayed[c];
            }
        } else {
            auto& active = *pipelines_[static_cast<size_t>(active_)];
            active.process(current, delayed, weights_, gains_, listen, meter_shares_);
            active.apply(delayed, output_spec_);
            if (fading_ >= 0) {
                auto& old = *pipelines_[static_cast<size_t>(fading_)];
                old.process(current, delayed, weights_, gains_, listen, meter_shares_fading_);
                old.apply(delayed, output_fading_);
                const auto mix = active.isWarm() ? static_cast<float>(++fade_pos_) / static_cast<float>(kFadeFrames) : 0.f;
                // the outputs fade, not the transfers: the two pipelines may not work on the same signals (L/R, M/S)
                for (size_t c = 0; c < static_cast<size_t>(num_channels_); ++c) {
                    auto& y = output_spec_[c];
                    const auto& y_old = output_fading_[c];
                    for (size_t k = 0; k < kBins; ++k) {
                        y[k] = y_old[k] + mix * (y[k] - y_old[k]);
                    }
                }
                if (mix < 1.f) {
                    meter_shares_ = meter_shares_fading_;
                } else {
                    fading_ = -1;
                }
            }
        }

        if (delayed_frame < 0) {
            return;
        }

        // synthesise and overlap-add: frame (frame - lag) completes output [(frame - lag - 1) * hop, (frame - lag) * hop)
        const auto out_begin = (delayed_frame - 1) * kHop;
        for (int c = 0; c < num_channels_; ++c) {
            fft_.inverse(output_spec_[static_cast<size_t>(c)], synth_);
            auto& ola = ola_[static_cast<size_t>(c)];
            for (size_t n = 0; n < kFftSize; ++n) {
                ola[n] += synth_[n] * window[n];
            }
            auto& out = out_ring_[static_cast<size_t>(c)];
            for (int n = 0; n < kHop; ++n) {
                const auto time = out_begin + n;
                if (time >= 0) {
                    out[static_cast<size_t>(time & ring_mask_)] = ola[static_cast<size_t>(n)];
                }
            }
            std::copy(ola.begin() + kHop, ola.end(), ola.begin());
            std::fill(ola.begin() + kFftSize - kHop, ola.end(), 0.f);
        }
        produced_.store(std::max<int64_t>(out_begin + kHop, 0), std::memory_order::release);

        publishAnalysis(*delayed[0], meter_shares_, output_spec_[0]);
    }

    void Engine::publishAnalysis(const Spectrum& input, const StemShares& shares, const Spectrum& output) {
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
            const auto energy = std::norm(input[k]) * kNorm * w.weight;
            frame.input[band] += energy;
            frame.output[band] += std::norm(output[k]) * kNorm * w.weight;
            for (size_t s = 0; s < kNumStems; ++s) {
                frame.stems[s][band] += energy * std::norm(shares.share[s][k]);
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
