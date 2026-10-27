// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <functional>

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/engine.hpp"

/**
 * test signals and measurements shared by the test files
 */
namespace signals {
    inline constexpr double kPi = juce::MathConstants<double>::pi;

    /**
     * a sustained vowel: a 160 Hz harmonic stack whose partials fall away like a voice's (bright, but its high
     * harmonics sit far below its body)
     */
    inline void addVowel(juce::AudioBuffer<float>& b, const int ch, const int start, const int length,
                         const double rate, const float peak) {
        auto* x = b.getWritePointer(ch);
        for (int i = 0; i < length && start + i < b.getNumSamples(); ++i) {
            const auto t = static_cast<double>(i) / rate;
            double v = 0.0;
            for (int h = 1; h * 160.0 < 10000.0; ++h) {
                v += std::sin(2.0 * kPi * 160.0 * h * t) / std::pow(static_cast<double>(h), 1.4);
            }
            // a 10 ms attack and release, like a voice's
            const auto fade = std::min({1.0, t / .01, static_cast<double>(length - i) / rate / .01});
            x[start + i] += static_cast<float>(.5 * peak * fade * v);
        }
    }

    /**
     * an ess: noise through a band-pass around `centre` (two state-variable sections), roughly `peak` at its peaks
     */
    inline void addEss(juce::AudioBuffer<float>& b, const int ch, const int start, const int length,
                       const double rate, const float peak, const double centre = 7000.0, const int seed = 7) {
        using namespace altvane::deesser::dsp;
        juce::Random random(seed);
        SVF f1, f2;
        const auto c = SVFCoeffs::make(centre, 1.2, rate);
        auto* x = b.getWritePointer(ch);
        // the two sections' gain, measured once for white noise: brings the peaks near `peak`
        for (int i = 0; i < length && start + i < b.getNumSamples(); ++i) {
            const auto white = random.nextFloat() * 2.f - 1.f;
            const auto y = c.k * f2.process(c.k * f1.process(white, c).bp, c).bp;
            const auto fade = std::min({1.f, static_cast<float>(i) / 200.f, static_cast<float>(length - i) / 200.f});
            x[start + i] += 1.6f * peak * fade * y;
        }
    }

    inline void addSine(juce::AudioBuffer<float>& b, const int ch, const int start, const int length,
                        const double rate, const double hz, const float amp) {
        auto* x = b.getWritePointer(ch);
        for (int i = 0; i < length && start + i < b.getNumSamples(); ++i) {
            x[start + i] += amp * static_cast<float>(std::sin(2.0 * kPi * hz * static_cast<double>(i) / rate));
        }
    }

    /**
     * the amplitude (dB) of one frequency over a stretch, by correlation
     */
    inline float toneDb(const juce::AudioBuffer<float>& b, const int ch, const int start, const int length,
                        const double rate, const double hz) {
        double re = 0.0, im = 0.0;
        const auto* x = b.getReadPointer(ch);
        for (int i = 0; i < length; ++i) {
            const auto w = 2.0 * kPi * hz * static_cast<double>(start + i) / rate;
            re += x[start + i] * std::cos(w);
            im += x[start + i] * std::sin(w);
        }
        return juce::Decibels::gainToDecibels(static_cast<float>(2.0 * std::sqrt(re * re + im * im) / length), -200.f);
    }

    /**
     * the level (dB RMS) above a frequency (fourth-order high-pass)
     */
    inline float highRmsDb(const juce::AudioBuffer<float>& b, const int ch, const int start, const int length,
                           const double rate, const double hz) {
        using namespace altvane::deesser::dsp;
        SVF f1, f2;
        const auto c1 = SVFCoeffs::make(hz, kButterworth4Q1, rate), c2 = SVFCoeffs::make(hz, kButterworth4Q2, rate);
        double sum = 0.0;
        int counted = 0;
        for (int i = 0; i < start + length; ++i) {
            const auto y = f2.process(f1.process(b.getSample(ch, i), c1).hp, c2).hp;
            if (i >= start) {
                sum += static_cast<double>(y) * y;
                ++counted;
            }
        }
        return juce::Decibels::gainToDecibels(static_cast<float>(std::sqrt(sum / std::max(1, counted))), -200.f);
    }

    inline float rmsDb(const juce::AudioBuffer<float>& b, const int ch, const int start, const int length) {
        return juce::Decibels::gainToDecibels(b.getRMSLevel(ch, start, length), -200.f);
    }

    /**
     * run a buffer through an engine in fixed blocks; the output is as long as the input plus the latency
     */
    inline juce::AudioBuffer<float> runEngine(altvane::deesser::dsp::Engine& engine,
                                              const altvane::deesser::dsp::Controls& controls,
                                              const juce::AudioBuffer<float>& input, const double rate,
                                              const juce::AudioBuffer<float>* side = nullptr, const int block = 256,
                                              const std::function<void()>& after_block = {}) {
        using altvane::deesser::dsp::Engine;
        engine.setControls(controls);
        engine.prepare(rate, input.getNumChannels());
        const auto latency = Engine::lookaheadSamples(controls.lookahead_ms, rate);
        const auto channels = input.getNumChannels();
        const auto total = input.getNumSamples() + latency;
        juce::AudioBuffer<float> out(channels, total);
        out.clear();
        for (int c = 0; c < channels; ++c) {
            out.copyFrom(c, 0, input, c, 0, input.getNumSamples());
        }
        juce::AudioBuffer<float> side_padded;
        if (side != nullptr) {
            side_padded.setSize(side->getNumChannels(), total);
            side_padded.clear();
            for (int c = 0; c < side->getNumChannels(); ++c) {
                side_padded.copyFrom(c, 0, *side, c, 0, side->getNumSamples());
            }
        }
        for (int pos = 0; pos < total; pos += block) {
            const auto n = std::min(block, total - pos);
            std::array<float*, 2> io{out.getWritePointer(0, pos), out.getWritePointer(std::min(1, channels - 1), pos)};
            std::array<const float*, 2> sc{};
            if (side != nullptr) {
                sc = {side_padded.getReadPointer(0, pos),
                      side_padded.getReadPointer(std::min(1, side_padded.getNumChannels() - 1), pos)};
            }
            engine.process(io.data(), channels, side != nullptr ? sc.data() : nullptr,
                           side != nullptr ? side_padded.getNumChannels() : 0, n);
            if (after_block) {
                after_block();
            }
        }
        return out;
    }
}
