// Copyright (C) 2026 - Altvane, based on ZLLimiter by zsliu98
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.hpp"

namespace test {
    inline constexpr double kSampleRate = 48000.0;
    inline constexpr int kBlock = 512;

    inline void set(juce::AudioProcessorValueTreeState& tree, const char* id, const float plain) {
        auto* param = tree.getParameter(id);
        REQUIRE(param != nullptr);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    inline void set(PluginProcessor& p, const char* id, const float plain) {
        set(p.parameters_, id, plain);
    }

    inline void prepare(PluginProcessor& p, const int block = kBlock) {
        p.setRateAndBufferSizeDetails(kSampleRate, block);
        p.prepareToPlay(kSampleRate, block);
    }

    inline void pumpMessages() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    /**
     * stereo signal: sample index -> value (same in both channels unless the generator says otherwise)
     */
    using Generator = std::function<std::pair<float, float>(int64_t)>;

    inline Generator sine(const double hz, const float peak_db, const double phase = 0.0) {
        const auto amp = static_cast<double>(juce::Decibels::decibelsToGain(peak_db));
        return [=](const int64_t n) {
            const auto v = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) /
                                                             kSampleRate + phase));
            return std::pair{v, v};
        };
    }

    /**
     * run the processor and collect both output channels; `before_block` sees the index of each block's first sample
     */
    inline std::array<std::vector<float>, 2> run(PluginProcessor& p, const Generator& gen, const double seconds,
                                                 const bool host_bypass = false, const int block = kBlock,
                                                 const std::function<void(int64_t)>& before_block = {}) {
        const auto total = static_cast<int64_t>(seconds * kSampleRate);
        std::array<std::vector<float>, 2> out;
        out[0].reserve(static_cast<size_t>(total));
        out[1].reserve(static_cast<size_t>(total));
        juce::AudioBuffer<float> buffer(2, block);
        juce::MidiBuffer midi;
        for (int64_t start = 0; start < total; start += block) {
            const auto n = static_cast<int>(std::min<int64_t>(block, total - start));
            if (before_block) {
                before_block(start);
            }
            buffer.setSize(2, n, false, false, true);
            for (int i = 0; i < n; ++i) {
                const auto [l, r] = gen(start + i);
                buffer.setSample(0, i, l);
                buffer.setSample(1, i, r);
            }
            if (host_bypass) {
                p.processBlockBypassed(buffer, midi);
            } else {
                p.processBlock(buffer, midi);
            }
            for (int i = 0; i < n; ++i) {
                out[0].push_back(buffer.getSample(0, i));
                out[1].push_back(buffer.getSample(1, i));
            }
        }
        return out;
    }

    inline float peakDB(const std::vector<float>& x, const size_t from = 0) {
        float peak = 0.f;
        for (auto i = from; i < x.size(); ++i) {
            peak = std::max(peak, std::abs(x[i]));
        }
        return juce::Decibels::gainToDecibels(peak, -200.f);
    }

    inline float rmsDB(const std::vector<float>& x, const size_t from = 0) {
        double sum = 0.0;
        for (auto i = from; i < x.size(); ++i) {
            sum += static_cast<double>(x[i]) * x[i];
        }
        return static_cast<float>(10.0 * std::log10(std::max(sum / static_cast<double>(x.size() - from), 1e-30)));
    }

    /**
     * the true peak of a signal, by 16x band-limited interpolation with a long Kaiser-windowed sinc
     * (an independent check: the plug-in's own estimator is shorter)
     */
    inline float truePeakDB(const std::vector<float>& x, const size_t from = 0) {
        constexpr int kOver = 16, kHalf = 64;
        const auto beta = 10.0;
        const auto bessel = [](const double v) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) {
                term *= (v / (2.0 * k)) * (v / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        std::vector<double> kernel(static_cast<size_t>(kOver * 2 * kHalf));
        for (int phase = 0; phase < kOver; ++phase) {
            const auto t = static_cast<double>(phase) / kOver;
            for (int k = -kHalf + 1; k <= kHalf; ++k) {
                const auto d = static_cast<double>(k) - t;
                const auto sinc = std::abs(d) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * d) / (std::numbers::pi * d);
                const auto r = d / kHalf;
                const auto window = std::abs(r) >= 1.0 ? 0.0 : bessel(beta * std::sqrt(1.0 - r * r)) / bessel(beta);
                kernel[static_cast<size_t>(phase * 2 * kHalf + k + kHalf - 1)] = sinc * window;
            }
        }
        float peak = 0.f;
        const auto n = static_cast<int64_t>(x.size());
        for (auto i = static_cast<int64_t>(from) + kHalf; i < n - kHalf; ++i) {
            for (int phase = 0; phase < kOver; ++phase) {
                const auto* h = kernel.data() + phase * 2 * kHalf;
                double sum = 0.0;
                for (int k = -kHalf + 1; k <= kHalf; ++k) {
                    sum += static_cast<double>(x[static_cast<size_t>(i + k)]) * h[k + kHalf - 1];
                }
                peak = std::max(peak, static_cast<float>(std::abs(sum)));
            }
        }
        return juce::Decibels::gainToDecibels(peak, -200.f);
    }
}
