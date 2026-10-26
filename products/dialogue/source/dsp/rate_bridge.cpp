// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "rate_bridge.hpp"

#include <numeric>
#include <CDSPResampler.h>

namespace altvane::dialogue::dsp {
    namespace {
        std::unique_ptr<r8b::CDSPResampler24> makeResampler(const double from, const double to, const int max_in) {
            return std::make_unique<r8b::CDSPResampler24>(from, to, max_in);
        }

        void delayLine(std::vector<float>& line, int& pos, const int delay, const float* in, float* out, const int n,
                       const bool advance) {
            const auto size = static_cast<int>(line.size());
            auto p = pos;
            for (int i = 0; i < n; ++i) {
                line[static_cast<size_t>(p)] = in[i];
                auto r = p - delay;
                if (r < 0) {
                    r += size;
                }
                out[i] = line[static_cast<size_t>(r)];
                p = p + 1 == size ? 0 : p + 1;
            }
            if (advance) {
                pos = p;
            }
        }
    }

    void RateBridge::Fifo::push(const double* samples, const int count) {
        if (read + size + count > static_cast<int>(data.size())) {
            std::copy(data.begin() + read, data.begin() + read + size, data.begin());
            read = 0;
        }
        jassert(read + size + count <= static_cast<int>(data.size()));
        const auto n = std::min(count, static_cast<int>(data.size()) - read - size);
        for (int i = 0; i < n; ++i) {
            data[static_cast<size_t>(read + size + i)] = static_cast<float>(samples[i]);
        }
        size += n;
    }

    void RateBridge::Fifo::push(const float* samples, const int count) {
        if (read + size + count > static_cast<int>(data.size())) {
            std::copy(data.begin() + read, data.begin() + read + size, data.begin());
            read = 0;
        }
        const auto n = std::min(count, static_cast<int>(data.size()) - read - size);
        std::copy(samples, samples + n, data.begin() + read + size);
        size += n;
    }

    void RateBridge::Fifo::pop(float* dest, const int count) {
        const auto n = std::min(count, size);
        std::copy(data.begin() + read, data.begin() + read + n, dest);
        // an underrun can only happen if the host exceeds the block size it announced
        std::fill(dest + n, dest + count, 0.f);
        read += n;
        size -= n;
    }

    void RateBridge::Fifo::prefill(const int count) {
        read = 0;
        size = count;
        std::fill(data.begin(), data.begin() + count, 0.f);
    }

    RateBridge::RateBridge() = default;

    RateBridge::~RateBridge() = default;

    int RateBridge::simulatePrefill(const int block) const {
        // how far the round trip's output runs behind a host that takes `block` samples every call
        auto down = makeResampler(sample_rate_, kSampleRate, max_block_);
        auto up = makeResampler(kSampleRate, sample_rate_, max_block_48_);
        std::vector<double> in(static_cast<size_t>(block), 0.0);
        int64_t produced = 0, consumed = 0, deficit = 0;
        const auto calls = static_cast<int>(sample_rate_ * 2.0) / block + 64;
        for (int i = 0; i < calls; ++i) {
            double* out48 = nullptr;
            const auto m = down->process(in.data(), block, out48);
            std::vector<double> mid(out48, out48 + m);
            double* out = nullptr;
            produced += up->process(mid.data(), m, out);
            consumed += block;
            deficit = std::max(deficit, consumed - produced);
        }
        return static_cast<int>(deficit);
    }

    bool RateBridge::prepare(const double sample_rate, const int num_channels, const int max_block) {
        sample_rate_ = sample_rate;
        num_channels_ = std::clamp(num_channels, 1, 2);
        max_block_ = std::max(max_block, 1);
        resampling_ = std::abs(sample_rate - kSampleRate) > .5;
        engine_ptrs_.assign(2, nullptr);

        bool ready;
        if (!resampling_) {
            max_block_48_ = max_block_;
            ready = engine_.prepare(num_channels_, max_block_);
            latency_ = engine_.getLatency();
        } else {
            // the engine's latency must map to a whole number of host samples
            const auto rate = static_cast<int>(std::round(sample_rate));
            const auto multiple = kSampleRate / std::gcd(kSampleRate, rate);
            // the largest burst the down-sampler can deliver in one call
            {
                auto down = makeResampler(sample_rate, kSampleRate, max_block_);
                std::vector<double> in(static_cast<size_t>(max_block_), 0.0);
                max_block_48_ = 1;
                for (int i = 0; i < static_cast<int>(sample_rate) / max_block_ + 64; ++i) {
                    double* out = nullptr;
                    max_block_48_ = std::max(max_block_48_, down->process(in.data(), max_block_, out));
                }
            }
            ready = engine_.prepare(num_channels_, max_block_48_, multiple);
            const auto chain = static_cast<int>(static_cast<int64_t>(engine_.getLatency()) * rate / kSampleRate);
            const auto prefill = std::max({simulatePrefill(max_block_), simulatePrefill(std::max(1, max_block_ / 3)),
                                           simulatePrefill(std::min(max_block_, 32))});
            latency_ = chain + prefill;

            const auto fifo_size = static_cast<size_t>(8 * (max_block_ + max_block_48_ + prefill) + 4096);
            for (size_t c = 0; c < 2; ++c) {
                down_[c] = makeResampler(sample_rate, kSampleRate, max_block_);
                up_[c] = makeResampler(kSampleRate, sample_rate, max_block_48_);
                up_dry_[c] = makeResampler(kSampleRate, sample_rate, max_block_48_);
                in_double_[c].assign(static_cast<size_t>(max_block_), 0.0);
                stage_[c].assign(static_cast<size_t>(max_block_48_), 0.f);
                stage_dry_[c].assign(static_cast<size_t>(max_block_48_), 0.f);
                stage_double_[c].assign(static_cast<size_t>(max_block_48_), 0.0);
                stage_dry_double_[c].assign(static_cast<size_t>(max_block_48_), 0.0);
                out_fifo_[c].data.assign(fifo_size, 0.f);
                round_trip_fifo_[c].data.assign(fifo_size, 0.f);
                out_fifo_[c].prefill(prefill);
                round_trip_fifo_[c].prefill(prefill);
                dry48_delay_[c].assign(static_cast<size_t>(engine_.getLatency() + max_block_48_ + 1), 0.f);
                round_trip_[c].assign(static_cast<size_t>(max_block_), 0.f);
            }
            dry48_delay_pos_ = 0;
        }
        for (size_t c = 0; c < 2; ++c) {
            host_delay_[c].assign(static_cast<size_t>(latency_ + max_block_ + 1), 0.f);
            dry_out_[c].assign(static_cast<size_t>(max_block_), 0.f);
        }
        host_delay_pos_ = 0;
        return ready;
    }

    void RateBridge::release() {
        engine_.release();
    }

    void RateBridge::process(juce::AudioBuffer<float>& buffer, const bool realtime, const float residual_gain) {
        const auto channels = std::min(num_channels_, buffer.getNumChannels());
        const auto n = buffer.getNumSamples();
        jassert(n <= max_block_);
        constexpr int offset = 0;
        {

            for (int c = 0; c < channels; ++c) {
                delayLine(host_delay_[static_cast<size_t>(c)], host_delay_pos_, latency_,
                          buffer.getReadPointer(c, offset), dry_out_[static_cast<size_t>(c)].data(), n,
                          c == channels - 1);
            }

            if (!resampling_) {
                for (int c = 0; c < channels; ++c) {
                    engine_ptrs_[static_cast<size_t>(c)] = buffer.getWritePointer(c, offset);
                }
                engine_.process(engine_ptrs_.data(), n, realtime);
                return;
            }

            int m = 0;
            for (int c = 0; c < channels; ++c) {
                auto& in = in_double_[static_cast<size_t>(c)];
                const auto* x = buffer.getReadPointer(c, offset);
                for (int i = 0; i < n; ++i) {
                    in[static_cast<size_t>(i)] = x[i];
                }
                double* out = nullptr;
                m = down_[static_cast<size_t>(c)]->process(in.data(), n, out);
                auto& stage = stage_[static_cast<size_t>(c)];
                for (int i = 0; i < m; ++i) {
                    stage[static_cast<size_t>(i)] = static_cast<float>(out[i]);
                }
                delayLine(dry48_delay_[static_cast<size_t>(c)], dry48_delay_pos_, engine_.getLatency(), stage.data(),
                          stage_dry_[static_cast<size_t>(c)].data(), m, c == channels - 1);
                engine_ptrs_[static_cast<size_t>(c)] = stage.data();
            }
            if (m > 0) {
                engine_.process(engine_ptrs_.data(), m, realtime);
            }
            for (int c = 0; c < channels; ++c) {
                const auto cs = static_cast<size_t>(c);
                for (int i = 0; i < m; ++i) {
                    stage_double_[cs][static_cast<size_t>(i)] = stage_[cs][static_cast<size_t>(i)];
                    stage_dry_double_[cs][static_cast<size_t>(i)] = stage_dry_[cs][static_cast<size_t>(i)];
                }
                double* out = nullptr;
                auto q = up_[cs]->process(stage_double_[cs].data(), m, out);
                out_fifo_[cs].push(out, q);
                q = up_dry_[cs]->process(stage_dry_double_[cs].data(), m, out);
                round_trip_fifo_[cs].push(out, q);

                auto* y = buffer.getWritePointer(c, offset);
                out_fifo_[cs].pop(y, n);
                round_trip_fifo_[cs].pop(round_trip_[cs].data(), n);
                const auto* dry = dry_out_[cs].data();
                const auto* rt = round_trip_[cs].data();
                for (int i = 0; i < n; ++i) {
                    y[i] += residual_gain * (dry[i] - rt[i]);
                }
            }
        }
    }
}
