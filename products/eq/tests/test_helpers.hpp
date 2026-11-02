// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
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

    inline void set(juce::AudioProcessorValueTreeState& tree, const juce::String& id, const float plain) {
        auto* param = tree.getParameter(id);
        REQUIRE(param != nullptr);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    inline void set(PluginProcessor& p, const juce::String& id, const float plain) {
        set(p.parameters_, id, plain);
    }

    /**
     * a band parameter, e.g. setBand(p, 2, zlp::PGain::kID, -3.f) for gain2
     */
    inline void setBand(PluginProcessor& p, const size_t band, const char* id, const float plain) {
        set(p, juce::String(id) + juce::String(band), plain);
    }

    inline void prepare(PluginProcessor& p, const int block = kBlock) {
        p.setRateAndBufferSizeDetails(kSampleRate, block);
        p.prepareToPlay(kSampleRate, block);
    }

    /**
     * runs the message loop briefly: the engine reports latency changes from it
     */
    inline void pumpMessages() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    /**
     * stereo signal: sample index -> value per channel
     */
    using Generator = std::function<std::pair<float, float>(int64_t)>;

    inline Generator sine(const double hz, const float peak_db) {
        const auto amp = static_cast<double>(juce::Decibels::decibelsToGain(peak_db));
        return [=](const int64_t n) {
            const auto v = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) /
                                                             kSampleRate));
            return std::pair{v, v};
        };
    }

    /**
     * a few tones and decorrelated noise, different in each channel, so every band and stereo mode has work
     */
    inline Generator music() {
        return [](const int64_t n) {
            const auto t = static_cast<double>(n) / kSampleRate;
            // a cheap deterministic hash for the noise, so a generator gives the same samples however it is called
            const auto noise = [](uint64_t x) {
                x ^= x >> 33;
                x *= 0xff51afd7ed558ccdULL;
                x ^= x >> 33;
                x *= 0xc4ceb9fe1a85ec53ULL;
                x ^= x >> 33;
                return static_cast<double>(x >> 11) / static_cast<double>(1ULL << 53) * 2.0 - 1.0;
            };
            const auto u = static_cast<uint64_t>(n);
            const auto tones = .2 * std::sin(2.0 * std::numbers::pi * 110.0 * t) +
                               .1 * std::sin(2.0 * std::numbers::pi * 3150.0 * t);
            const auto envelope = .5 + .5 * std::sin(2.0 * std::numbers::pi * 1.5 * t);
            return std::pair{static_cast<float>(tones + .1 * envelope * noise(2 * u)),
                             static_cast<float>(.8 * tones + .1 * noise(2 * u + 1))};
        };
    }

    /**
     * runs the processor over a signal in blocks (sizes from `blocks`, cycled) and collects both output channels;
     * the side-chain input gets the same signal as the main input
     */
    template <typename FloatType = float>
    std::array<std::vector<float>, 2> run(PluginProcessor& p, const Generator& gen, const double seconds,
                                          const std::vector<int>& blocks = {kBlock}, const bool host_bypass = false) {
        const auto total = static_cast<int64_t>(seconds * kSampleRate);
        const auto channels = p.getTotalNumInputChannels();
        std::array<std::vector<float>, 2> out;
        out[0].reserve(static_cast<size_t>(total));
        out[1].reserve(static_cast<size_t>(total));
        juce::AudioBuffer<FloatType> buffer(channels, *std::max_element(blocks.begin(), blocks.end()));
        juce::MidiBuffer midi;
        size_t b = 0;
        for (int64_t start = 0; start < total;) {
            const auto n = static_cast<int>(std::min<int64_t>(blocks[b++ % blocks.size()], total - start));
            buffer.setSize(channels, n, false, false, true);
            for (int i = 0; i < n; ++i) {
                const auto [l, r] = gen(start + i);
                for (int c = 0; c < channels; ++c) {
                    buffer.setSample(c, i, static_cast<FloatType>(c % 2 == 0 ? l : r));
                }
            }
            if (host_bypass) {
                p.processBlockBypassed(buffer, midi);
            } else {
                p.processBlock(buffer, midi);
            }
            for (int i = 0; i < n; ++i) {
                out[0].push_back(static_cast<float>(buffer.getSample(0, i)));
                out[1].push_back(static_cast<float>(buffer.getSample(1, i)));
            }
            start += n;
        }
        return out;
    }

    inline float worstDifference(const std::array<std::vector<float>, 2>& a, const std::array<std::vector<float>, 2>& b,
                                 const size_t from = 0) {
        REQUIRE(a[0].size() == b[0].size());
        float worst = 0.f;
        for (size_t c = 0; c < 2; ++c) {
            for (auto n = from; n < a[c].size(); ++n) {
                worst = std::max(worst, std::abs(a[c][n] - b[c][n]));
            }
        }
        return worst;
    }

    inline float peakDB(const std::vector<float>& x, const size_t from = 0) {
        float peak = 0.f;
        for (auto i = from; i < x.size(); ++i) {
            peak = std::max(peak, std::abs(x[i]));
        }
        return juce::Decibels::gainToDecibels(peak, -200.f);
    }
}
