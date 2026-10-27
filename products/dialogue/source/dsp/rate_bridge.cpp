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

namespace altvane::dialogue::dsp {
    namespace {
        // the largest polyphase factor we accept (44.1 kHz needs 160, 22.05 kHz 320)
        constexpr int64_t kMaxPhases = 2048;

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
        // cannot happen with the prefill prepare() computes
        std::fill(dest + n, dest + count, 0.f);
        read += n;
        size -= n;
    }

    void RateBridge::Fifo::prefill(const int count) {
        read = 0;
        size = count;
        std::fill(data.begin(), data.begin() + count, 0.f);
    }

    bool RateBridge::prepare(const double sample_rate, const int num_channels, const int max_block, const bool stems) {
        num_channels_ = std::clamp(num_channels, 1, 2);
        max_block_ = std::max(max_block, 1);
        stems_ = stems;
        engine_ptrs_.assign(2, nullptr);
        const auto rate = static_cast<int>(std::round(sample_rate));
        resampling_ = rate != kSampleRate;
        const auto common = Resampler::commonRate(rate, kSampleRate);
        supported_ = rate > 0 && common / rate <= kMaxPhases && common / kSampleRate <= kMaxPhases;

        bool ready = false;
        if (!supported_) {
            engine_.release();
            latency_ = 0;
        } else if (!resampling_) {
            max_block_48_ = max_block_;
            ready = engine_.prepare(num_channels_, max_block_, 1, stems_);
            latency_ = engine_.getLatency();
        } else {
            // both directions use one prototype length K at the common rate R; the round trip delays (K - 1) / R
            // seconds, a whole number q of host samples since K - 1 = q * R / rate
            const auto host_phase = common / rate;
            auto q = std::max(32, static_cast<int>(std::ceil(32.0 * rate / kSampleRate)));
            q += q % 2;
            const auto prototype = static_cast<int>(q * host_phase + 1);
            const auto cutoff = .45 * std::min(rate, kSampleRate);
            // and the engine's delay must map to whole host samples too
            const auto multiple = static_cast<int>(kSampleRate / std::gcd(kSampleRate, rate));

            max_block_48_ = static_cast<int>((static_cast<int64_t>(max_block_) * kSampleRate + rate - 1) / rate) + 2;
            ready = engine_.prepare(num_channels_, max_block_48_, multiple, stems_);
            const auto engine_delay = static_cast<int>(static_cast<int64_t>(engine_.getLatency()) * rate / kSampleRate);
            // outputs arrive one input sample at a time, so the host can run at most a couple of samples ahead
            constexpr int kPrefill = 2;
            latency_ = engine_delay + q + kPrefill;

            const auto fifo_size = static_cast<size_t>(4 * (max_block_ + kPrefill) + 64);
            for (size_t c = 0; c < 2; ++c) {
                down_[c].prepare(rate, kSampleRate, prototype, cutoff);
                up_[c].prepare(kSampleRate, rate, prototype, cutoff);
                up_dry_[c].prepare(kSampleRate, rate, prototype, cutoff);
                stage_[c].assign(static_cast<size_t>(max_block_48_ + 4), 0.f);
                stage_dry_[c].assign(static_cast<size_t>(max_block_48_ + 4), 0.f);
                up_out_[c].assign(static_cast<size_t>(up_[c].maxOutput(max_block_48_ + 4)), 0.f);
                out_fifo_[c].data.assign(fifo_size, 0.f);
                round_trip_fifo_[c].data.assign(fifo_size, 0.f);
                out_fifo_[c].prefill(kPrefill);
                round_trip_fifo_[c].prefill(kPrefill);
                dry48_delay_[c].assign(static_cast<size_t>(engine_.getLatency() + max_block_48_ + 8), 0.f);
                round_trip_[c].assign(static_cast<size_t>(max_block_), 0.f);
                for (size_t s = 0; s < kNumStems; ++s) {
                    if (stems_) {
                        up_stem_[s][c].prepare(kSampleRate, rate, prototype, cutoff);
                    }
                    stem_stage_[s][c].assign(stems_ ? static_cast<size_t>(max_block_48_ + 4) : 0, 0.f);
                    stem_fifo_[s][c].data.assign(stems_ ? fifo_size : 0, 0.f);
                    stem_fifo_[s][c].prefill(stems_ ? kPrefill : 0);
                }
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

    void RateBridge::process(juce::AudioBuffer<float>& buffer, const bool realtime, const float residual_gain,
                             const StemOutputs* stems, const float stem_residual_gain) {
        const auto channels = std::min(num_channels_, buffer.getNumChannels());
        const auto n = buffer.getNumSamples();
        jassert(n <= max_block_);
        if (!stems_) {
            stems = nullptr;
        }

        for (int c = 0; c < channels; ++c) {
            delayLine(host_delay_[static_cast<size_t>(c)], host_delay_pos_, latency_, buffer.getReadPointer(c),
                      dry_out_[static_cast<size_t>(c)].data(), n, c == channels - 1);
        }
        if (!supported_) {
            // the output is the input, so all of it is voice
            if (stems != nullptr) {
                for (size_t s = 0; s < kNumStems; ++s) {
                    for (int c = 0; c < channels; ++c) {
                        auto* dest = (*stems)[s][static_cast<size_t>(c)];
                        if (s == kVoice) {
                            std::copy(buffer.getReadPointer(c), buffer.getReadPointer(c) + n, dest);
                        } else {
                            std::fill(dest, dest + n, 0.f);
                        }
                    }
                }
            }
            return;
        }
        if (!resampling_) {
            for (int c = 0; c < channels; ++c) {
                engine_ptrs_[static_cast<size_t>(c)] = buffer.getWritePointer(c);
            }
            engine_.process(engine_ptrs_.data(), n, realtime, stems);
            return;
        }

        int m = 0;
        for (int c = 0; c < channels; ++c) {
            const auto cs = static_cast<size_t>(c);
            m = down_[cs].process(buffer.getReadPointer(c), n, stage_[cs].data());
            delayLine(dry48_delay_[cs], dry48_delay_pos_, engine_.getLatency(), stage_[cs].data(),
                      stage_dry_[cs].data(), m, c == channels - 1);
            engine_ptrs_[cs] = stage_[cs].data();
            for (size_t s = 0; s < kNumStems; ++s) {
                stem_ptrs_[s][cs] = stems != nullptr ? stem_stage_[s][cs].data() : nullptr;
            }
        }
        if (m > 0) {
            engine_.process(engine_ptrs_.data(), m, realtime, stems != nullptr ? &stem_ptrs_ : nullptr);
        }
        for (int c = 0; c < channels; ++c) {
            const auto cs = static_cast<size_t>(c);
            auto q = up_[cs].process(stage_[cs].data(), m, up_out_[cs].data());
            out_fifo_[cs].push(up_out_[cs].data(), q);
            q = up_dry_[cs].process(stage_dry_[cs].data(), m, up_out_[cs].data());
            round_trip_fifo_[cs].push(up_out_[cs].data(), q);

            auto* y = buffer.getWritePointer(c);
            out_fifo_[cs].pop(y, n);
            round_trip_fifo_[cs].pop(round_trip_[cs].data(), n);
            const auto* dry = dry_out_[cs].data();
            const auto* rt = round_trip_[cs].data();
            for (int i = 0; i < n; ++i) {
                y[i] += residual_gain * (dry[i] - rt[i]);
            }

            if (stems != nullptr) {
                for (size_t s = 0; s < kNumStems; ++s) {
                    q = up_stem_[s][cs].process(stem_stage_[s][cs].data(), m, up_out_[cs].data());
                    stem_fifo_[s][cs].push(up_out_[cs].data(), q);
                    auto* dest = (*stems)[s][cs];
                    stem_fifo_[s][cs].pop(dest, n);
                    if (s == kAmbience) {
                        for (int i = 0; i < n; ++i) {
                            dest[i] += stem_residual_gain * (dry[i] - rt[i]);
                        }
                    }
                }
            }
        }
    }
}
