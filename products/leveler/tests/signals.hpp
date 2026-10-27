// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.hpp"

/**
 * synthetic material for the tests: a sung vowel, breaths, sibilants and a music key, at known loudness
 */
namespace test_signals {
    using altvane::leveler::PluginProcessor;
    namespace dsp = altvane::leveler::dsp;
    namespace params = altvane::leveler::params;

    inline constexpr double kPi = juce::MathConstants<double>::pi;

    inline void setParam(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    /**
     * BS.1770 loudness (ungated) of a span, channels summed
     */
    inline float loudness(const juce::AudioBuffer<float>& buffer, const double rate, const int start, const int end) {
        double sum = 0.0;
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            dsp::KFilter k;
            k.prepare(rate);
            for (int i = 0; i < end; ++i) {
                const auto y = k.process(buffer.getSample(c, i));
                if (i >= start) {
                    sum += y * y;
                }
            }
        }
        return dsp::loudnessDb(sum / std::max(end - start, 1));
    }

    /**
     * scale the span so its loudness is `lufs` (measured over `measure_start`.. of it, after the filters settle)
     */
    inline void setLoudness(juce::AudioBuffer<float>& buffer, const double rate, const int start, const int end,
                            const float lufs) {
        juce::AudioBuffer<float> span(buffer.getNumChannels(), end - start);
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            span.copyFrom(c, 0, buffer, c, start, end - start);
        }
        const auto now = loudness(span, rate, 0, span.getNumSamples());
        const auto gain = juce::Decibels::decibelsToGain(lufs - now);
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            buffer.applyGain(c, start, end - start, gain);
        }
    }

    /**
     * a sung vowel (harmonics through two formants, with vibrato); `syllables` cuts it into 200 ms notes with
     * 100 ms gaps
     */
    inline void addVoice(juce::AudioBuffer<float>& buffer, const double rate, const int start, const int end,
                         const float lufs, const bool syllables, const double f0 = 180.0) {
        juce::AudioBuffer<float> voice(buffer.getNumChannels(), end - start);
        double phase = 0.0;
        for (int i = 0; i < voice.getNumSamples(); ++i) {
            const auto t = static_cast<double>(i) / rate;
            phase += f0 * (1.0 + .01 * std::sin(2.0 * kPi * 5.0 * t)) / rate;
            double x = 0.0;
            for (int h = 1; h * f0 < std::min(rate * .45, 5000.0); ++h) {
                const auto f = h * f0;
                const auto formant = std::exp(-std::pow((f - 700.0) / 400.0, 2.0)) + .6 * std::exp(-std::pow((f - 2300.0) / 600.0, 2.0));
                x += std::sin(2.0 * kPi * phase * h) * (.15 + formant) / h;
            }
            auto envelope = 1.0;
            if (syllables) {
                const auto u = std::fmod(t, .3);
                envelope = u < .2 ? std::min(1.0, std::min(u, .2 - u) / .01) : 0.0;
            }
            for (int c = 0; c < voice.getNumChannels(); ++c) {
                voice.setSample(c, i, static_cast<float>(x * envelope));
            }
        }
        // set the loudness of the sung parts (for syllables: 2/3 of the time)
        setLoudness(voice, rate, 0, voice.getNumSamples(), syllables ? lufs + 10.f * std::log10(2.f / 3.f) : lufs);
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            buffer.addFrom(c, start, voice, c, 0, voice.getNumSamples());
        }
    }

    /**
     * noise through a filter: a breath (low-passed at 5 kHz) or a sibilant (high-passed at 5 kHz)
     */
    inline void addNoise(juce::AudioBuffer<float>& buffer, const double rate, const int start, const int end,
                         const float lufs, const bool sibilant, const int seed = 7) {
        juce::AudioBuffer<float> noise(buffer.getNumChannels(), end - start);
        juce::Random random(seed);
        dsp::Biquad a, b;
        const auto make = sibilant ? dsp::highPass : dsp::lowPass;
        a.setCoefficients(make(rate, 5000.0, .54));
        b.setCoefficients(make(rate, 5000.0, 1.31));
        for (int i = 0; i < noise.getNumSamples(); ++i) {
            const auto ramp = std::min(1.0, std::min(i, noise.getNumSamples() - i) / (.02 * rate));
            const auto x = static_cast<float>(ramp * b.process(a.process(random.nextFloat() * 2.f - 1.f)));
            for (int c = 0; c < noise.getNumChannels(); ++c) {
                noise.setSample(c, i, x);
            }
        }
        setLoudness(noise, rate, 0, noise.getNumSamples(), lufs);
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            buffer.addFrom(c, start, noise, c, 0, noise.getNumSamples());
        }
    }

    /**
     * a music bed: a chord with some noise, at a loudness
     */
    inline void addMusic(juce::AudioBuffer<float>& buffer, const double rate, const int start, const int end,
                         const float lufs) {
        juce::AudioBuffer<float> music(buffer.getNumChannels(), end - start);
        juce::Random random(11);
        for (int i = 0; i < music.getNumSamples(); ++i) {
            const auto t = static_cast<double>(i) / rate;
            double x = .1 * (random.nextDouble() * 2.0 - 1.0);
            for (const auto f : {110.0, 220.0, 277.2, 329.6, 440.0}) {
                x += std::sin(2.0 * kPi * f * t) * .3;
            }
            for (int c = 0; c < music.getNumChannels(); ++c) {
                music.setSample(c, i, static_cast<float>(x));
            }
        }
        setLoudness(music, rate, 0, music.getNumSamples(), lufs);
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            buffer.addFrom(c, start, music, c, 0, music.getNumSamples());
        }
    }

    inline int samples(const double rate, const double seconds) {
        return static_cast<int>(std::lround(rate * seconds));
    }

    struct Run {
        juce::AudioBuffer<float> output;
        std::vector<dsp::Column> columns;
        int latency{0};
        int hop{0};
    };

    /**
     * run the input (and key, if any) through the processor in blocks (sizes cycled), flushing the latency
     * and collecting one history column per hop
     */
    inline Run run(PluginProcessor& p, const juce::AudioBuffer<float>& input, const double rate,
                   const std::vector<int>& blocks = {512}, const juce::AudioBuffer<float>* key = nullptr) {
        const auto channels = input.getNumChannels();
        const auto key_channels = key != nullptr ? key->getNumChannels() : 0;
        const auto max_block = *std::max_element(blocks.begin(), blocks.end());
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::canonicalChannelSet(channels);
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::canonicalChannelSet(channels);
        layout.inputBuses.getReference(1) = key_channels > 0 ? juce::AudioChannelSet::canonicalChannelSet(key_channels)
                                                             : juce::AudioChannelSet::disabled();
        REQUIRE(p.setBusesLayout(layout));
        p.setRateAndBufferSizeDetails(rate, max_block);
        p.setNonRealtime(true);
        p.prepareToPlay(rate, max_block);
        auto& engine = p.getEngine();
        engine.setPublishing(true);
        Run result;
        result.latency = p.getLatencySamples();
        result.hop = engine.getHopSamples();
        const auto total = input.getNumSamples() + result.latency;
        result.output.setSize(channels, total);
        juce::AudioBuffer<float> io(channels + key_channels, max_block);
        juce::MidiBuffer midi;
        size_t b = 0;
        dsp::Column column;
        for (int pos = 0; pos < total;) {
            const auto n = std::min(blocks[b++ % blocks.size()], total - pos);
            io.setSize(channels + key_channels, n, false, false, true);
            io.clear();
            const auto available = std::clamp(input.getNumSamples() - pos, 0, n);
            for (int c = 0; c < channels && available > 0; ++c) {
                io.copyFrom(c, 0, input, c, pos, available);
            }
            for (int c = 0; c < key_channels && available > 0; ++c) {
                io.copyFrom(channels + c, 0, *key, c, pos, available);
            }
            p.processBlock(io, midi);
            for (int c = 0; c < channels; ++c) {
                result.output.copyFrom(c, pos, io, c, 0, n);
            }
            while (engine.popColumn(column)) {
                result.columns.push_back(column);
            }
            pos += n;
        }
        engine.setPublishing(false);
        p.releaseResources();
        return result;
    }

    /**
     * the column describing the input at a time (columns are lined up with the output, a latency later),
     * with the gain that was applied to it
     */
    inline const dsp::Column& columnAt(const Run& r, const double rate, const double seconds) {
        const auto idx = static_cast<size_t>(std::max(0.0, (seconds * rate + r.latency) / r.hop - 1.0));
        return r.columns[std::min(idx, r.columns.size() - 1)];
    }

    /**
     * the lowest and highest gain applied to a span of the input
     */
    inline std::pair<float, float> gainRange(const Run& r, const double rate, const double from, const double to) {
        auto lo = 1e9f, hi = -1e9f;
        for (auto t = from; t <= to; t += r.hop / rate) {
            const auto g = columnAt(r, rate, t).gain_db;
            lo = std::min(lo, g);
            hi = std::max(hi, g);
        }
        return {lo, hi};
    }
}
