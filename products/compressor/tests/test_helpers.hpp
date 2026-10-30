// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
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

    inline float get(PluginProcessor& p, const juce::String& id) {
        return p.parameters_.getRawParameterValue(id)->load();
    }

    /**
     * main and side-chain layouts: 1 or 2 main channels, 0 (disabled), 1 or 2 side-chain channels
     */
    inline void setLayout(PluginProcessor& p, const int main, const int aux) {
        auto layout = p.getBusesLayout();
        const auto set_of = [](const int n) {
            return n == 0 ? juce::AudioChannelSet::disabled()
                          : n == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        };
        layout.inputBuses.getReference(0) = set_of(main);
        layout.outputBuses.getReference(0) = set_of(main);
        layout.inputBuses.getReference(1) = set_of(aux);
        REQUIRE(p.setBusesLayout(layout));
    }

    inline void prepare(PluginProcessor& p, const int block = kBlock) {
        p.setRateAndBufferSizeDetails(kSampleRate, block);
        p.prepareToPlay(kSampleRate, block);
    }

    inline void pumpMessages() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }

    /**
     * sample index -> value, the same in every channel of its bus
     */
    using Generator = std::function<float(int64_t)>;

    inline Generator sine(const double hz, const float peak_db) {
        const auto amp = static_cast<double>(juce::Decibels::decibelsToGain(peak_db));
        return [=](const int64_t n) {
            return static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / kSampleRate));
        };
    }

    inline Generator silence() {
        return [](int64_t) { return 0.f; };
    }

    /**
     * drums-like bursts: a decaying 120 Hz tone every quarter second with some noise, so the gain moves
     */
    inline Generator bursts() {
        return [](const int64_t n) {
            const auto t = static_cast<double>(n % 12000) / kSampleRate;
            const auto k = static_cast<double>(n);
            const auto noise = .3 * std::sin(k * 1.7) * std::cos(k * .31);
            return static_cast<float>((.6 * std::sin(2.0 * std::numbers::pi * 120.0 * t) + noise) * std::exp(-t * 14.0));
        };
    }

    struct Run {
        Generator main, side{silence()};
        int64_t start{0};
        bool bypassed{false};
        // the block sizes the host sends, in turn
        std::vector<int> blocks{kBlock};
    };

    /**
     * run the processor over [start, start + length) of the generators and return the main output channels
     */
    template <typename T = float>
    std::vector<std::vector<T>> run(PluginProcessor& p, const Run& r, const int64_t length) {
        const auto main_channels = p.getMainBusNumOutputChannels();
        const auto* aux = p.getBus(true, 1);
        const auto aux_channels = aux != nullptr && aux->isEnabled() ? aux->getNumberOfChannels() : 0;
        const auto total_channels = std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        std::vector<std::vector<T>> out(static_cast<size_t>(main_channels));
        juce::AudioBuffer<T> buffer;
        juce::MidiBuffer midi;
        size_t block_index = 0;
        for (int64_t done = 0; done < length;) {
            const auto n = static_cast<int>(std::min<int64_t>(r.blocks[block_index++ % r.blocks.size()], length - done));
            buffer.setSize(total_channels, n, false, false, true);
            buffer.clear();
            for (int i = 0; i < n; ++i) {
                const auto at = r.start + done + i;
                for (int c = 0; c < main_channels; ++c) {
                    buffer.setSample(c, i, static_cast<T>(r.main(at)));
                }
                for (int c = 0; c < aux_channels; ++c) {
                    buffer.setSample(main_channels + c, i, static_cast<T>(r.side(at)));
                }
            }
            if (r.bypassed) {
                p.processBlockBypassed(buffer, midi);
            } else {
                p.processBlock(buffer, midi);
            }
            for (int c = 0; c < main_channels; ++c) {
                out[static_cast<size_t>(c)].insert(out[static_cast<size_t>(c)].end(), buffer.getReadPointer(c),
                                                   buffer.getReadPointer(c) + n);
            }
            done += n;
        }
        return out;
    }

    inline int64_t seconds(const double s) {
        return static_cast<int64_t>(s * kSampleRate);
    }

    template <typename T>
    float peakDB(const std::vector<T>& x, const size_t from = 0) {
        double peak = 0.0;
        for (auto i = from; i < x.size(); ++i) {
            peak = std::max(peak, std::abs(static_cast<double>(x[i])));
        }
        return juce::Decibels::gainToDecibels(static_cast<float>(peak), -240.f);
    }
}
