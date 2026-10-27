// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginProcessor.hpp"

using Catch::Matchers::WithinAbs;

namespace {
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 512;

    void set(PluginProcessor& p, const char* id, const float plain) {
        auto* param = p.parameters_.getParameter(id);
        REQUIRE(param != nullptr);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    float get(PluginProcessor& p, const char* id) {
        return p.parameters_.getRawParameterValue(id)->load();
    }

    void prepare(PluginProcessor& p) {
        p.setRateAndBufferSizeDetails(kSampleRate, kBlock);
        p.prepareToPlay(kSampleRate, kBlock);
    }

    /**
     * a static compressor: linked left/right detection, hard knee, instant attack, slow release
     * so that a steady sine settles to the curve's gain at its peak level
     */
    void makeStatic(PluginProcessor& p, const float threshold, const float ratio) {
        set(p, zlp::PSideStereoMode::kID, 1.f);
        set(p, zlp::PSideStereoLink::kID, 100.f);
        set(p, zlp::PKneeW::kID, 0.f);
        set(p, zlp::PAttack::kID, 0.f);
        set(p, zlp::PRelease::kID, 2000.f);
        set(p, zlp::PThreshold::kID, threshold);
        set(p, zlp::PRatio::kID, ratio);
    }

    /**
     * runs a 1 kHz sine at the given peak level through the processor for the given time
     * and returns the output peak (dBFS) over the last half second
     */
    float runSine(PluginProcessor& p, const float peak_db, const double seconds, const bool bypassed = false) {
        juce::AudioBuffer<float> buffer(4, kBlock);
        juce::MidiBuffer midi;
        const auto amp = juce::Decibels::decibelsToGain(peak_db);
        const auto blocks = static_cast<int>(seconds * kSampleRate / kBlock);
        const auto measure_from = blocks - static_cast<int>(.5 * kSampleRate / kBlock);
        double phase = 0.0;
        float out_peak = 0.f;
        for (int b = 0; b < blocks; ++b) {
            buffer.clear();
            for (int i = 0; i < kBlock; ++i) {
                const auto v = amp * static_cast<float>(std::sin(phase));
                phase += juce::MathConstants<double>::twoPi * 1000.0 / kSampleRate;
                buffer.setSample(0, i, v);
                buffer.setSample(1, i, v);
            }
            juce::AudioBuffer<float> io(buffer.getArrayOfWritePointers(), p.getTotalNumInputChannels(), kBlock);
            if (bypassed) {
                p.processBlockBypassed(io, midi);
            } else {
                p.processBlock(io, midi);
            }
            if (b >= measure_from) {
                out_peak = std::max(out_peak, io.getMagnitude(0, 0, kBlock));
            }
        }
        return juce::Decibels::gainToDecibels(out_peak, -240.f);
    }

    void pumpMessages() {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }
}

TEST_CASE("state round trip keeps every parameter", "[state]") {
    PluginProcessor a;
    set(a, zlp::PThreshold::kID, -21.5f);
    set(a, zlp::PRatio::kID, 6.f);
    set(a, zlp::PCompDirection::kID, 2.f);
    set(a, zlp::PSideGain::kID, 4.f);
    set(a, "freq3", 2500.f);
    juce::MemoryBlock block;
    a.getStateInformation(block);

    PluginProcessor b;
    b.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK_THAT(get(b, zlp::PThreshold::kID), WithinAbs(-21.5, .05));
    CHECK_THAT(get(b, zlp::PRatio::kID), WithinAbs(6.0, .01));
    CHECK_THAT(get(b, zlp::PCompDirection::kID), WithinAbs(2.0, 1e-6));
    CHECK_THAT(get(b, zlp::PSideGain::kID), WithinAbs(4.0, .01));
    CHECK_THAT(get(b, "freq3"), WithinAbs(2500.0, 1.0));
}

TEST_CASE("state is written with ZL Compressor's tags", "[state]") {
    // the root and child tags ZL Compressor writes, so existing sessions and presets carry over
    PluginProcessor a;
    set(a, zlp::PThreshold::kID, -12.f);
    juce::MemoryBlock block;
    a.getStateInformation(block);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName(zlstate::schema::kProcessorState));
    CHECK(xml->getChildByName(zlstate::schema::kParameterState) != nullptr);
    CHECK(xml->getChildByName(zlstate::schema::kNonAutomatableState) != nullptr);
}

TEST_CASE("a steady tone settles on the compression curve", "[dsp]") {
    PluginProcessor p;
    makeStatic(p, -30.f, 4.f);
    prepare(p);
    // -10 dBFS is 20 dB over the threshold: at 4:1 the output sits 15 dB lower
    const auto out = runSine(p, -10.f, 2.0);
    CHECK_THAT(out, WithinAbs(-25.0, .5));

    // below the threshold nothing happens
    PluginProcessor quiet;
    makeStatic(quiet, -30.f, 4.f);
    prepare(quiet);
    CHECK_THAT(runSine(quiet, -40.f, 1.5), WithinAbs(-40.0, .1));
}

TEST_CASE("makeup and range shape the output", "[dsp]") {
    PluginProcessor p;
    makeStatic(p, -30.f, 4.f);
    set(p, zlp::PRange::kID, 6.f);
    set(p, zlp::POutGain::kID, 3.f);
    prepare(p);
    // the 15 dB of reduction is capped at 6 dB, then 3 dB of makeup
    CHECK_THAT(runSine(p, -10.f, 2.0), WithinAbs(-13.0, .5));
}

TEST_CASE("host bypass passes audio unchanged", "[dsp]") {
    PluginProcessor p;
    makeStatic(p, -40.f, 10.f);
    set(p, zlp::POutGain::kID, 6.f);
    prepare(p);
    CHECK_THAT(runSine(p, -6.f, 1.5, true), WithinAbs(-6.0, .05));
}

TEST_CASE("latency follows lookahead and oversampling", "[latency]") {
    PluginProcessor p;
    prepare(p);
    runSine(p, -20.f, .1);
    pumpMessages();
    CHECK(p.getLatencySamples() == 0);

    set(p, zlp::PLookAhead::kID, 5.f);
    runSine(p, -20.f, .1);
    pumpMessages();
    const auto lookahead = p.getLatencySamples();
    CHECK(std::abs(lookahead - 240) <= 1);

    set(p, zlp::POversample::kID, 1.f);
    runSine(p, -20.f, .1);
    pumpMessages();
    CHECK(p.getLatencySamples() > lookahead);

    // a negative lookahead delays the side chain instead, which costs no latency
    set(p, zlp::POversample::kID, 0.f);
    set(p, zlp::PLookAhead::kID, -5.f);
    runSine(p, -20.f, .1);
    pumpMessages();
    CHECK(p.getLatencySamples() == 0);
}

TEST_CASE("listening to a stereo side chain on a mono track", "[routing]") {
    // mono main, stereo aux: the output hears the mid of the external side chain
    PluginProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference(0) = juce::AudioChannelSet::mono();
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
    layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
    REQUIRE(p.setBusesLayout(layout));
    set(p, zlp::PExtSide::kID, 1.f);
    set(p, zlp::PSideOut::kID, 1.f);
    prepare(p);

    juce::AudioBuffer<float> buffer(3, kBlock);
    juce::MidiBuffer midi;
    float out_peak = 0.f;
    for (int b = 0; b < 20; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            buffer.setSample(0, i, 0.f);                                                // main: silence
            buffer.setSample(1, i, .5f * std::sin(.13f * static_cast<float>(b * kBlock + i))); // aux left
            buffer.setSample(2, i, 0.f);                                                // aux right
        }
        p.processBlock(buffer, midi);
        if (b > 10) {
            out_peak = std::max(out_peak, buffer.getMagnitude(0, 0, kBlock));
        }
    }
    // mid = (left + right) / 2
    CHECK_THAT(out_peak, WithinAbs(.25, .01));
}
