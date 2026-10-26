// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "PluginProcessor.hpp"
#include "alloc_counter.hpp"

using namespace altvane::dialogue;

namespace {
    void setParam(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    void setLevels(PluginProcessor& p, const float voice, const float ambience, const float reverb) {
        setParam(p, params::kVoice, voice);
        setParam(p, params::kAmbience, ambience);
        setParam(p, params::kReverb, reverb);
    }

    /**
     * speech-like test signal: harmonic bursts with pauses, over noise
     */
    juce::AudioBuffer<float> makeSignal(const int channels, const int length, const double rate, const int seed = 1) {
        juce::AudioBuffer<float> buffer(channels, length);
        juce::Random random(seed);
        for (int c = 0; c < channels; ++c) {
            auto* x = buffer.getWritePointer(c);
            double phase = 0.0;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i) / rate;
                const auto syllable = std::fmod(t, .25) < .17 ? 1.0 : 0.0;
                const auto f0 = 140.0 + 30.0 * std::sin(2.0 * juce::MathConstants<double>::pi * 1.3 * t);
                phase += f0 / rate;
                double voice = 0.0;
                for (int h = 1; h < 12; ++h) {
                    voice += std::sin(2.0 * juce::MathConstants<double>::pi * phase * h) / h;
                }
                x[i] = static_cast<float>(.2 * syllable * voice + .03 * (random.nextFloat() * 2.f - 1.f));
            }
        }
        return buffer;
    }

    struct Run {
        juce::AudioBuffer<float> output;
        int latency{0};
    };

    /**
     * run a buffer through the processor in blocks (sizes from `blocks`, cycled), flushing the latency
     */
    Run run(PluginProcessor& p, const juce::AudioBuffer<float>& input, const double rate,
            const std::vector<int>& blocks, const bool realtime) {
        const auto channels = input.getNumChannels();
        const auto max_block = *std::max_element(blocks.begin(), blocks.end());
        p.setPlayConfigDetails(channels, channels, rate, max_block);
        p.setNonRealtime(!realtime);
        p.prepareToPlay(rate, max_block);
        Run result;
        result.latency = p.getLatencySamples();
        const auto total = input.getNumSamples() + result.latency;
        result.output.setSize(channels, total);
        juce::AudioBuffer<float> io(channels, max_block);
        juce::MidiBuffer midi;
        size_t b = 0;
        for (int pos = 0; pos < total;) {
            const auto n = std::min(blocks[b++ % blocks.size()], total - pos);
            io.setSize(channels, n, false, false, true);
            for (int c = 0; c < channels; ++c) {
                io.clear(c, 0, n);
                const auto available = std::clamp(input.getNumSamples() - pos, 0, n);
                if (available > 0) {
                    io.copyFrom(c, 0, input, c, pos, available);
                }
            }
            p.processBlock(io, midi);
            for (int c = 0; c < channels; ++c) {
                result.output.copyFrom(c, pos, io, c, 0, n);
            }
            pos += n;
        }
        p.releaseResources();
        return result;
    }

    /**
     * the largest difference between the output and the input delayed by the latency, in dB relative to full scale
     */
    float nullResidualDb(const Run& r, const juce::AudioBuffer<float>& input) {
        float worst = 0.f;
        for (int c = 0; c < input.getNumChannels(); ++c) {
            for (int i = 0; i < input.getNumSamples(); ++i) {
                worst = std::max(worst, std::abs(r.output.getSample(c, i + r.latency) - input.getSample(c, i)));
            }
        }
        return juce::Decibels::gainToDecibels(worst, -200.f);
    }
}

TEST_CASE("with every level at unity the output is the input, delayed by the reported latency", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    const auto channels = GENERATE(1, 2);
    CAPTURE(rate, channels);
    PluginProcessor p;
    setLevels(p, 0.f, 0.f, 0.f);
    const auto input = makeSignal(channels, static_cast<int>(rate * 1.5), rate);
    // irregular block sizes, as hosts deliver them
    const auto r = run(p, input, rate, {512, 7, 300, 1, 512, 64, 333}, false);
    CHECK(nullResidualDb(r, input) < -90.f);
}

TEST_CASE("the real-time path is transparent even when the model falls behind", "[engine]") {
    PluginProcessor p;
    setLevels(p, 0.f, 0.f, 0.f);
    const auto input = makeSignal(2, 48000, 48000.0);
    // calls back to back, far faster than real time: many hops arrive late and fall back to the delayed input
    const auto r = run(p, input, 48000.0, {256}, true);
    CHECK(nullResidualDb(r, input) < -90.f);
}

TEST_CASE("the reported latency is the delay of an impulse", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    CAPTURE(rate);
    PluginProcessor p;
    setLevels(p, 0.f, 0.f, 0.f);
    juce::AudioBuffer<float> input(1, static_cast<int>(rate));
    input.clear();
    input.setSample(0, 1000, 1.f);
    const auto r = run(p, input, rate, {480}, false);
    int peak = 0;
    for (int i = 0; i < r.output.getNumSamples(); ++i) {
        if (std::abs(r.output.getSample(0, i)) > std::abs(r.output.getSample(0, peak))) {
            peak = i;
        }
    }
    CHECK(peak == 1000 + r.latency);
}

TEST_CASE("the 48 kHz round trip lines up with the delayed input", "[engine]") {
    // with no residual, the output is up(down(x)): in band it must match the delayed input sample for sample,
    // or the residual would comb-filter whenever the ambience level is not unity
    const auto rate = GENERATE(44100.0, 88200.0, 96000.0);
    CAPTURE(rate);
    dsp::RateBridge bridge;
    constexpr int kBlock = 300;
    bridge.prepare(rate, 1, kBlock);
    juce::AudioBuffer<float> io(1, kBlock);
    std::vector<float> in, out;
    for (int b = 0; b < static_cast<int>(rate) / kBlock; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const auto t = static_cast<double>(b * kBlock + i) / rate;
            const auto x = static_cast<float>(.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * t)
                                              + .3 * std::sin(2.0 * juce::MathConstants<double>::pi * 7300.0 * t));
            io.setSample(0, i, x);
            in.push_back(x);
        }
        bridge.process(io, false, 0.f);
        out.insert(out.end(), io.getReadPointer(0), io.getReadPointer(0) + kBlock);
    }
    bridge.release();
    float worst = 0.f;
    for (size_t i = static_cast<size_t>(bridge.getLatency()) + 4800; i < out.size(); ++i) {
        worst = std::max(worst, std::abs(out[i] - in[i - static_cast<size_t>(bridge.getLatency())]));
    }
    CHECK(juce::Decibels::gainToDecibels(worst) < -60.f);
}

TEST_CASE("the solo stems add up to the input", "[engine]") {
    PluginProcessor p;
    setLevels(p, -6.f, -20.f, -10.f);
    const auto input = makeSignal(1, 48000 * 2, 48000.0);
    juce::AudioBuffer<float> sum(1, input.getNumSamples() + 8192);
    sum.clear();
    int latency = 0;
    for (const auto listen : {dsp::Listen::kVoice, dsp::Listen::kAmbience, dsp::Listen::kReverb}) {
        p.setListen(listen);
        const auto r = run(p, input, 48000.0, {512}, false);
        latency = r.latency;
        sum.addFrom(0, 0, r.output, 0, 0, r.output.getNumSamples());
    }
    Run combined{sum, latency};
    CHECK(nullResidualDb(combined, input) < -90.f);
}

TEST_CASE("offline output does not depend on the block size", "[engine]") {
    PluginProcessor p;
    setLevels(p, 0.f, -30.f, -12.f);
    const auto input = makeSignal(2, 48000 * 2, 48000.0);
    const auto a = run(p, input, 48000.0, {512}, false);
    const auto b = run(p, input, 48000.0, {64, 1000, 17}, false);
    float worst = 0.f;
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < input.getNumSamples(); ++i) {
            worst = std::max(worst, std::abs(a.output.getSample(c, i + a.latency) - b.output.getSample(c, i + b.latency)));
        }
    }
    CHECK(worst < 1e-6f);
}

TEST_CASE("lowering ambience and reverb removes energy, and Removed plays what is gone", "[engine]") {
    PluginProcessor p;
    const auto input = makeSignal(1, 48000 * 2, 48000.0);
    setLevels(p, 0.f, -60.f, -60.f);
    const auto mix = run(p, input, 48000.0, {512}, false);
    p.setListen(dsp::Listen::kRemoved);
    const auto removed = run(p, input, 48000.0, {512}, false);
    double in_energy = 0.0, mix_energy = 0.0;
    float worst = 0.f;
    for (int i = 24000; i < input.getNumSamples(); ++i) {
        const auto x = input.getSample(0, i);
        const auto y = mix.output.getSample(0, i + mix.latency);
        in_energy += x * x;
        mix_energy += y * y;
        worst = std::max(worst, std::abs(y + removed.output.getSample(0, i + removed.latency) - x));
    }
    CHECK(mix_energy < in_energy * .95);
    CHECK(worst < 1e-4f);
}

TEST_CASE("the audio thread neither allocates nor locks", "[engine]") {
    PluginProcessor p;
    setLevels(p, 0.f, -20.f, -10.f);
    p.setPlayConfigDetails(2, 2, 48000.0, 512);
    p.setNonRealtime(false);
    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> io(2, 512);
    juce::MidiBuffer midi;
    const auto input = makeSignal(2, 512, 48000.0);
    size_t allocations = 0;
    for (int block = 0; block < 400; ++block) {
        io.makeCopyOf(input, true);
        const alloc_counter::Scope scope;
        p.processBlock(io, midi);
        allocations += scope.count();
    }
    p.releaseResources();
    CHECK(allocations == 0);
}

TEST_CASE("parameter IDs never change", "[params]") {
    // saved sessions and presets refer to these; this list may only grow
    const juce::StringArray frozen{
        "voice_level", "ambience_level", "reverb_level", "quality", "stereo_mode", "output_gain", "bypass",
        "voice_sens_1", "voice_sens_2", "voice_sens_3", "voice_sens_4", "voice_sens_5", "voice_sens_6",
        "ambience_sens_1", "ambience_sens_2", "ambience_sens_3", "ambience_sens_4", "ambience_sens_5",
        "ambience_sens_6",
        "reverb_sens_1", "reverb_sens_2", "reverb_sens_3", "reverb_sens_4", "reverb_sens_5", "reverb_sens_6"
    };
    PluginProcessor p;
    for (const auto& id : frozen) {
        CAPTURE(id);
        CHECK(p.parameters_.getParameter(id) != nullptr);
    }
    CHECK(params::allIDs() == frozen);
}

TEST_CASE("state round-trips", "[params]") {
    PluginProcessor a;
    setLevels(a, -3.f, -18.f, -40.f);
    setParam(a, params::sensitivityID(dsp::kAmbience, 2), .5f);
    setParam(a, params::kQuality, 0.f);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    PluginProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto& id : params::allIDs()) {
        CAPTURE(id);
        CHECK(std::abs(a.parameters_.getParameter(id)->getValue() - b.parameters_.getParameter(id)->getValue()) < 1e-6f);
    }
}

TEST_CASE("level text round-trips, with Off at the bottom", "[params]") {
    PluginProcessor p;
    auto* voice = p.parameters_.getParameter(params::kVoice);
    CHECK(voice->getText(0.f, 32) == "Off");
    CHECK(params::levelToGain(params::kLevelOff) == 0.f);
    CHECK(std::abs(voice->getValueForText("Off")) < 1e-6f);
    CHECK(std::abs(voice->convertFrom0to1(voice->getValueForText("-6.0 dB")) + 6.f) < .05f);
    // what the interface shows reads back
    const auto shown = voice->getText(voice->convertTo0to1(-6.f), 32);
    CHECK(std::abs(voice->convertFrom0to1(voice->getValueForText(shown)) + 6.f) < .05f);
}
