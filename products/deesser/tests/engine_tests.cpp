// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "PluginProcessor.hpp"
#include "dsp/learner.hpp"
#include "alloc_counter.hpp"
#include "signals.hpp"

using namespace altvane::deesser;
using namespace signals;

namespace {
    constexpr double kRate = 48000.0;

    /**
     * a phrase: vowels and esses at varied levels, over a little noise
     */
    juce::AudioBuffer<float> makePhrase(const int channels, const double rate, const float ess_peak = .25f) {
        const auto len = static_cast<int>(rate * 2.0);
        juce::AudioBuffer<float> b(channels, len);
        b.clear();
        const auto ms = [rate](const double m) { return static_cast<int>(m * .001 * rate); };
        for (int c = 0; c < channels; ++c) {
            addVowel(b, c, ms(0), ms(300), rate, .4f);
            addEss(b, c, ms(320), ms(140), rate, ess_peak, 6800.0, 3 + c);
            addVowel(b, c, ms(480), ms(400), rate, .5f);
            addEss(b, c, ms(900), ms(180), rate, ess_peak * 1.6f, 7600.0, 9 + c);
            addVowel(b, c, ms(1100), ms(500), rate, .3f);
            addEss(b, c, ms(1620), ms(120), rate, ess_peak * .7f, 6200.0, 13 + c);
            addSine(b, c, 0, len, rate, 90.0, .01f);
        }
        return b;
    }

    float worstDifference(const juce::AudioBuffer<float>& a, const int a_offset, const juce::AudioBuffer<float>& b,
                          const int b_offset, const int length) {
        float worst = 0.f;
        for (int c = 0; c < a.getNumChannels(); ++c) {
            for (int i = 0; i < length; ++i) {
                worst = std::max(worst, std::abs(a.getSample(c, a_offset + i) - b.getSample(c, b_offset + i)));
            }
        }
        return worst;
    }

    void setParam(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    /**
     * run a buffer through the processor in blocks (sizes from `blocks`, cycled), flushing the latency
     */
    juce::AudioBuffer<float> runProcessor(PluginProcessor& p, const juce::AudioBuffer<float>& input, const double rate,
                                          const std::vector<int>& blocks) {
        const auto channels = input.getNumChannels();
        const auto max_block = *std::max_element(blocks.begin(), blocks.end());
        p.setPlayConfigDetails(channels, channels, rate, max_block);
        p.prepareToPlay(rate, max_block);
        const auto total = input.getNumSamples() + p.getLatencySamples();
        juce::AudioBuffer<float> out(channels, total);
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
                out.copyFrom(c, pos, io, c, 0, n);
            }
            pos += n;
        }
        p.releaseResources();
        return out;
    }
}

TEST_CASE("with nothing to reduce the output is the input, delayed by the look-ahead", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    const auto processing = GENERATE(dsp::Processing::kWide, dsp::Processing::kSplit);
    const auto shape = GENERATE(dsp::Shape::kShelf, dsp::Shape::kBell);
    const auto stereo = GENERATE(dsp::StereoMode::kStereo, dsp::StereoMode::kMid, dsp::StereoMode::kSide);
    CAPTURE(rate, static_cast<int>(processing), static_cast<int>(shape), static_cast<int>(stereo));
    dsp::Controls c;
    c.threshold_db = 0.f;
    c.processing = processing;
    c.shape = shape;
    c.stereo = stereo;
    const auto input = makePhrase(2, rate, .05f);
    dsp::Engine engine;
    const auto out = runEngine(engine, c, input, rate);
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, rate);
    // bit-exact in stereo; mid/side encoding and decoding costs a rounding step
    CHECK(worstDifference(out, latency, input, 0, input.getNumSamples()) <= (stereo == dsp::StereoMode::kStereo ? 0.f : 1e-6f));
}

TEST_CASE("the reported latency is the look-ahead, and follows it", "[engine]") {
    PluginProcessor p;
    setParam(p, params::kLookahead, 5.f);
    // an impulse is all treble: keep it from being de-essed
    setParam(p, params::kThreshold, 0.f);
    juce::AudioBuffer<float> impulse(2, 4800);
    impulse.clear();
    impulse.setSample(0, 100, 1.f);
    impulse.setSample(1, 100, 1.f);
    auto out = runProcessor(p, impulse, kRate, {256});
    CHECK(p.getLatencySamples() == 240);
    CHECK(std::abs(out.getSample(0, 340) - 1.f) < 1e-6f);

    setParam(p, params::kLookahead, 10.f);
    CHECK(p.getLatencySamples() == 480);
    out = runProcessor(p, impulse, kRate, {256});
    CHECK(std::abs(out.getSample(0, 580) - 1.f) < 1e-6f);

    // a change while playing fades to the new delay, after which the impulse lands where reported
    {
        p.setPlayConfigDetails(2, 2, kRate, 256);
        p.prepareToPlay(kRate, 256);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> io(2, 256);
        setParam(p, params::kLookahead, 2.f);
        const auto latency = p.getLatencySamples();
        CHECK(latency == 96);
        std::vector<float> response;
        for (int block = 0; block < 40; ++block) {
            io.clear();
            if (block == 20) {
                io.setSample(0, 0, .5f);
            }
            p.processBlock(io, midi);
            for (int i = 0; i < 256; ++i) {
                response.push_back(io.getSample(0, i));
            }
        }
        CHECK(std::abs(response[static_cast<size_t>(20 * 256 + latency)] - .5f) < 1e-6f);
        p.releaseResources();
    }
}

TEST_CASE("an ess above the threshold is brought down to it, never by more than the range", "[engine]") {
    const auto mode = GENERATE(dsp::Mode::kVocal, dsp::Mode::kAllround);
    CAPTURE(static_cast<int>(mode));
    const auto len = static_cast<int>(kRate * .6);
    juce::AudioBuffer<float> input(1, len);
    input.clear();
    addEss(input, 0, 0, len, kRate, .3f);

    // the band level the detector sees
    dsp::Detector detector;
    detector.prepare(kRate);
    dsp::Detector::Filters filters;
    filters.hp1 = dsp::SVFCoeffs::make(5500.0, dsp::kButterworth4Q1, kRate);
    filters.hp2 = dsp::SVFCoeffs::make(5500.0, dsp::kButterworth4Q2, kRate);
    filters.body = dsp::SVFCoeffs::make(5500.0, dsp::kButterworth2Q, kRate);
    std::vector<float> band_db;
    for (int i = 0; i < len; ++i) {
        band_db.push_back(dsp::gainToDb(detector.process(input.getSample(0, i), filters, mode).level));
    }
    auto sorted = std::vector<float>(band_db.begin() + len / 4, band_db.end() - len / 4);
    std::sort(sorted.begin(), sorted.end());
    const auto typical = sorted[sorted.size() / 2];

    dsp::Controls c;
    c.mode = mode;
    c.processing = dsp::Processing::kWide;
    c.threshold_db = typical - 12.f;
    c.range_db = 24.f;
    dsp::Engine engine;
    auto out = runEngine(engine, c, input, kRate);
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, kRate);
    const auto start = len / 3, length = len / 3;
    const auto reduction = rmsDb(input, 0, start, length) - rmsDb(out, 0, start + latency, length);
    CHECK(reduction > 9.f);
    CHECK(reduction < 14.f);

    c.range_db = 4.f;
    out = runEngine(engine, c, input, kRate);
    const auto limited = rmsDb(input, 0, start, length) - rmsDb(out, 0, start + latency, length);
    CHECK(limited > 3.f);
    CHECK(limited < 4.3f);
}

TEST_CASE("split band leaves the lows alone; wide band turns everything down", "[engine]") {
    const auto len = static_cast<int>(kRate * .5);
    juce::AudioBuffer<float> input(1, len);
    input.clear();
    addEss(input, 0, 0, len, kRate, .3f);
    addSine(input, 0, 0, len, kRate, 300.0, .2f);
    dsp::Controls c;
    c.mode = dsp::Mode::kAllround;
    c.threshold_db = -40.f;
    c.range_db = 12.f;
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, kRate);
    const auto start = len / 3, length = len / 3;
    const auto before = toneDb(input, 0, start, length, kRate, 300.0);
    dsp::Engine engine;

    c.processing = dsp::Processing::kSplit;
    c.shape = dsp::Shape::kShelf;
    auto out = runEngine(engine, c, input, kRate);
    CHECK(std::abs(toneDb(out, 0, start + latency, length, kRate, 300.0) - before) < .3f);
    CHECK(highRmsDb(out, 0, start + latency, length, kRate, 5000.0) < highRmsDb(input, 0, start, length, kRate, 5000.0) - 6.f);

    c.processing = dsp::Processing::kWide;
    out = runEngine(engine, c, input, kRate);
    CHECK(toneDb(out, 0, start + latency, length, kRate, 300.0) < before - 6.f);
}

TEST_CASE("the bell turns down the band and spares what lies above it", "[engine]") {
    const auto len = static_cast<int>(kRate * .5);
    juce::AudioBuffer<float> input(1, len);
    input.clear();
    addEss(input, 0, 0, len, kRate, .3f, 6300.0);
    addSine(input, 0, 0, len, kRate, 16000.0, .03f);
    dsp::Controls c;
    c.mode = dsp::Mode::kAllround;
    c.threshold_db = -40.f;
    c.range_db = 12.f;
    c.band_low = 5000.f;
    c.band_high = 8000.f;
    c.band_open = false;
    c.processing = dsp::Processing::kSplit;
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, kRate);
    const auto start = len / 3, length = len / 3;
    const auto top = toneDb(input, 0, start, length, kRate, 16000.0);
    dsp::Engine engine;

    c.shape = dsp::Shape::kBell;
    auto out = runEngine(engine, c, input, kRate);
    CHECK(std::abs(toneDb(out, 0, start + latency, length, kRate, 16000.0) - top) < 1.f);
    CHECK(rmsDb(out, 0, start + latency, length) < rmsDb(input, 0, start, length) - 3.f);

    c.shape = dsp::Shape::kShelf;
    out = runEngine(engine, c, input, kRate);
    CHECK(toneDb(out, 0, start + latency, length, kRate, 16000.0) < top - 6.f);
}

TEST_CASE("Vocal mode lets a bright vowel through; Allround does not", "[engine]") {
    const auto len = static_cast<int>(kRate * .6);
    juce::AudioBuffer<float> input(1, len);
    input.clear();
    addVowel(input, 0, 0, len, kRate, .6f);
    dsp::Controls c;
    c.threshold_db = -60.f;
    c.range_db = 24.f;
    c.processing = dsp::Processing::kWide;
    dsp::Engine engine;

    c.mode = dsp::Mode::kVocal;
    runEngine(engine, c, input, kRate);
    CHECK(engine.takeReductionPeak() < .5f);

    c.mode = dsp::Mode::kAllround;
    runEngine(engine, c, input, kRate);
    CHECK(engine.takeReductionPeak() > 6.f);

    // and Vocal mode still catches the esses of a phrase
    c.mode = dsp::Mode::kVocal;
    c.threshold_db = -40.f;
    runEngine(engine, c, makePhrase(1, kRate), kRate);
    CHECK(engine.takeReductionPeak() > 6.f);
}

TEST_CASE("delta is exactly what the de-esser takes away", "[engine]") {
    const auto input = makePhrase(2, kRate);
    dsp::Controls c;
    c.threshold_db = -40.f;
    dsp::Engine engine;
    const auto processed = runEngine(engine, c, input, kRate);
    c.listen = dsp::Listen::kDelta;
    const auto delta = runEngine(engine, c, input, kRate);
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, kRate);
    float worst = 0.f, delta_peak = 0.f;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < input.getNumSamples(); ++i) {
            const auto sum = processed.getSample(ch, i + latency) + delta.getSample(ch, i + latency);
            worst = std::max(worst, std::abs(sum - input.getSample(ch, i)));
            delta_peak = std::max(delta_peak, std::abs(delta.getSample(ch, i + latency)));
        }
    }
    CHECK(worst < 1e-5f);
    CHECK(delta_peak > .05f);
}

TEST_CASE("Trigger plays the detection band", "[engine]") {
    const auto len = static_cast<int>(kRate * .5);
    juce::AudioBuffer<float> input(1, len);
    input.clear();
    addSine(input, 0, 0, len, kRate, 300.0, .3f);
    addSine(input, 0, 0, len, kRate, 9000.0, .1f);
    dsp::Controls c;
    c.threshold_db = 0.f;
    c.listen = dsp::Listen::kTrigger;
    dsp::Engine engine;
    const auto out = runEngine(engine, c, input, kRate);
    const auto start = len / 3, length = len / 3;
    CHECK(toneDb(out, 0, start, length, kRate, 300.0) < toneDb(input, 0, start, length, kRate, 300.0) - 60.f);
    CHECK(std::abs(toneDb(out, 0, start, length, kRate, 9000.0) - toneDb(input, 0, start, length, kRate, 9000.0)) < 1.f);
}

TEST_CASE("stereo link shares the reduction between the channels", "[engine]") {
    const auto len = static_cast<int>(kRate * .5);
    juce::AudioBuffer<float> input(2, len);
    input.clear();
    addEss(input, 0, 0, len, kRate, .3f);
    addSine(input, 1, 0, len, kRate, 300.0, .2f);
    dsp::Controls c;
    c.mode = dsp::Mode::kAllround;
    c.processing = dsp::Processing::kWide;
    c.threshold_db = -40.f;
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, kRate);
    const auto start = len / 3, length = len / 3;
    const auto right = toneDb(input, 1, start, length, kRate, 300.0);
    dsp::Engine engine;

    c.link = 1.f;
    auto out = runEngine(engine, c, input, kRate);
    const auto left_reduction = rmsDb(input, 0, start, length) - rmsDb(out, 0, start + latency, length);
    const auto right_reduction = right - toneDb(out, 1, start + latency, length, kRate, 300.0);
    CHECK(left_reduction > 6.f);
    CHECK(std::abs(left_reduction - right_reduction) < .5f);

    c.link = 0.f;
    out = runEngine(engine, c, input, kRate);
    CHECK(std::abs(toneDb(out, 1, start + latency, length, kRate, 300.0) - right) < .05f);
}

TEST_CASE("mid processing leaves the side untouched", "[engine]") {
    const auto len = static_cast<int>(kRate * .5);
    juce::AudioBuffer<float> mid(1, len), side(1, len), input(2, len);
    mid.clear();
    side.clear();
    addEss(mid, 0, 0, len, kRate, .3f);
    addEss(side, 0, 0, len, kRate, .2f, 8000.0, 99);
    for (int i = 0; i < len; ++i) {
        input.setSample(0, i, mid.getSample(0, i) + side.getSample(0, i));
        input.setSample(1, i, mid.getSample(0, i) - side.getSample(0, i));
    }
    dsp::Controls c;
    c.mode = dsp::Mode::kAllround;
    c.processing = dsp::Processing::kWide;
    c.threshold_db = -40.f;
    c.stereo = dsp::StereoMode::kMid;
    dsp::Engine engine;
    const auto out = runEngine(engine, c, input, kRate);
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, kRate);
    float worst_side = 0.f;
    for (int i = 0; i < len; ++i) {
        const auto s = .5f * (out.getSample(0, i + latency) - out.getSample(1, i + latency));
        worst_side = std::max(worst_side, std::abs(s - side.getSample(0, i)));
    }
    CHECK(worst_side < 1e-5f);
    CHECK(engine.takeReductionPeak() > 6.f);
}

TEST_CASE("switching to mid and back starts the right channel afresh", "[engine]") {
    // the right channel's state must not survive an excursion into mid processing
    const auto len = static_cast<int>(kRate * .3);
    dsp::Engine engine;
    dsp::Controls c;
    c.mode = dsp::Mode::kAllround;
    c.processing = dsp::Processing::kWide;
    c.threshold_db = -40.f;
    engine.setControls(c);
    engine.prepare(kRate, 2);
    juce::AudioBuffer<float> ess(2, len), tone(2, len);
    ess.clear();
    tone.clear();
    addEss(ess, 0, 0, len, kRate, .3f);
    addEss(ess, 1, 0, len, kRate, .3f, 7000.0, 11);
    addSine(tone, 0, 0, len, kRate, 300.0, .2f);
    addSine(tone, 1, 0, len, kRate, 300.0, .2f);
    auto play = [&](juce::AudioBuffer<float> b, const dsp::StereoMode mode) {
        c.stereo = mode;
        engine.setControls(c);
        for (int pos = 0; pos < len; pos += 256) {
            std::array<float*, 2> io{b.getWritePointer(0, pos), b.getWritePointer(1, pos)};
            engine.process(io.data(), 2, nullptr, 0, std::min(256, len - pos));
        }
        return b;
    };
    // stop in the middle of an ess, switch to mid with nothing sibilant, then back
    play(ess, dsp::StereoMode::kStereo);
    play(tone, dsp::StereoMode::kMid);
    const auto out = play(tone, dsp::StereoMode::kStereo);
    // the 300 Hz tone is not turned down once the 5 ms look-ahead has passed
    const auto start = static_cast<int>(kRate * .01);
    CHECK(std::abs(toneDb(out, 1, start, len - start, kRate, 300.0) - toneDb(tone, 1, start, len - start, kRate, 300.0)) < .1f);
}

TEST_CASE("an external side chain drives the reduction", "[engine]") {
    PluginProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
    REQUIRE(p.setBusesLayout(layout));
    REQUIRE(p.hasSideChainInput());
    setParam(p, params::kSideChain, 1.f);
    setParam(p, params::kProcessing, 0.f);
    setParam(p, params::kMode, 1.f);
    setParam(p, params::kThreshold, -40.f);
    p.prepareToPlay(kRate, 512);
    const auto latency = p.getLatencySamples();
    juce::AudioBuffer<float> io(4, 512);
    juce::AudioBuffer<float> main(2, 512 * 40), out(2, 512 * 40);
    main.clear();
    addSine(main, 0, 0, main.getNumSamples(), kRate, 300.0, .2f);
    addSine(main, 1, 0, main.getNumSamples(), kRate, 300.0, .2f);
    juce::AudioBuffer<float> ess(1, main.getNumSamples());
    ess.clear();
    addEss(ess, 0, 0, ess.getNumSamples(), kRate, .3f);
    juce::MidiBuffer midi;
    for (int b = 0; b < 40; ++b) {
        io.copyFrom(0, 0, main, 0, b * 512, 512);
        io.copyFrom(1, 0, main, 1, b * 512, 512);
        io.copyFrom(2, 0, ess, 0, b * 512, 512);
        io.copyFrom(3, 0, ess, 0, b * 512, 512);
        p.processBlock(io, midi);
        out.copyFrom(0, b * 512, io, 0, 0, 512);
        out.copyFrom(1, b * 512, io, 1, 0, 512);
    }
    const auto start = 512 * 10, length = 512 * 20;
    CHECK(toneDb(out, 0, start + latency, length, kRate, 300.0) < toneDb(main, 0, start, length, kRate, 300.0) - 6.f);
    p.releaseResources();
}

TEST_CASE("a mono external key drives mid and side processing", "[engine]") {
    for (const auto mode : {dsp::StereoMode::kSide, dsp::StereoMode::kMid}) {
        const auto side = mode == dsp::StereoMode::kSide;
        DYNAMIC_SECTION((side ? "side" : "mid")) {
            PluginProcessor p;
            auto layout = p.getBusesLayout();
            layout.inputBuses.getReference(1) = juce::AudioChannelSet::mono();
            REQUIRE(p.setBusesLayout(layout));
            setParam(p, params::kSideChain, 1.f);
            setParam(p, params::kStereoMode, static_cast<float>(mode));
            setParam(p, params::kProcessing, 0.f);
            setParam(p, params::kMode, 1.f);
            setParam(p, params::kThreshold, -40.f);
            p.prepareToPlay(kRate, 512);
            const auto latency = p.getLatencySamples();
            juce::AudioBuffer<float> io(3, 512);
            // only side (left against right) or only mid in the main signal, so the processed part is all of it
            juce::AudioBuffer<float> main(2, 512 * 40), out(2, 512 * 40);
            main.clear();
            addSine(main, 0, 0, main.getNumSamples(), kRate, 300.0, .2f);
            addSine(main, 1, 0, main.getNumSamples(), kRate, 300.0, side ? -.2f : .2f);
            juce::AudioBuffer<float> ess(1, main.getNumSamples());
            ess.clear();
            addEss(ess, 0, 0, ess.getNumSamples(), kRate, .3f);
            juce::MidiBuffer midi;
            for (int b = 0; b < 40; ++b) {
                io.copyFrom(0, 0, main, 0, b * 512, 512);
                io.copyFrom(1, 0, main, 1, b * 512, 512);
                io.copyFrom(2, 0, ess, 0, b * 512, 512);
                p.processBlock(io, midi);
                out.copyFrom(0, b * 512, io, 0, 0, 512);
                out.copyFrom(1, b * 512, io, 1, 0, 512);
            }
            const auto start = 512 * 10, length = 512 * 20;
            CHECK(toneDb(out, 0, start + latency, length, kRate, 300.0) < toneDb(main, 0, start, length, kRate, 300.0) - 6.f);
            p.releaseResources();
        }
    }
}

TEST_CASE("the output does not depend on the block size", "[engine]") {
    const auto input = makePhrase(2, kRate);
    PluginProcessor a, b;
    for (auto* p : {&a, &b}) {
        setParam(*p, params::kThreshold, -40.f);
        setParam(*p, params::kShape, 1.f);
    }
    const auto one = runProcessor(a, input, kRate, {512});
    const auto other = runProcessor(b, input, kRate, {1, 7, 64, 333, 1024, 31});
    CHECK(worstDifference(one, 0, other, 0, one.getNumSamples()) == 0.f);
}

TEST_CASE("the audio thread does not allocate", "[engine]") {
    PluginProcessor p;
    setParam(p, params::kThreshold, -40.f);
    p.setPlayConfigDetails(2, 2, kRate, 512);
    p.prepareToPlay(kRate, 512);
    p.getEngine().setFeedsActive(true);
    juce::AudioBuffer<float> io(2, 512);
    juce::MidiBuffer midi;
    const auto input = makePhrase(2, kRate);
    size_t allocations = 0;
    for (int block = 0; block < 180; ++block) {
        io.copyFrom(0, 0, input, 0, (block * 512) % (input.getNumSamples() - 512), 512);
        io.copyFrom(1, 0, input, 1, (block * 512) % (input.getNumSamples() - 512), 512);
        if (block == 90) {
            // look-ahead and listen changes too
            setParam(p, params::kLookahead, 12.f);
            p.setListen(dsp::Listen::kDelta);
        }
        const alloc_counter::Scope scope;
        p.processBlock(io, midi);
        allocations += scope.count();
    }
    p.releaseResources();
    CHECK(allocations == 0);
}

TEST_CASE("a host reset clears what the look-ahead held", "[engine]") {
    PluginProcessor p;
    setParam(p, params::kThreshold, -40.f);
    p.setPlayConfigDetails(2, 2, kRate, 512);
    p.prepareToPlay(kRate, 512);
    REQUIRE(p.getLatencySamples() > 0);
    juce::AudioBuffer<float> io(2, 512);
    juce::MidiBuffer midi;
    const auto input = makePhrase(2, kRate);
    for (int b = 0; b < 20; ++b) {
        io.copyFrom(0, 0, input, 0, b * 512, 512);
        io.copyFrom(1, 0, input, 1, b * 512, 512);
        p.processBlock(io, midi);
    }
    // the host stops and starts again without preparing: the next passage starts from silence
    p.reset();
    io.clear();
    p.processBlock(io, midi);
    CHECK(io.getMagnitude(0, 512) == 0.f);
    p.releaseResources();
}

TEST_CASE("bypass passes the input through with the same latency", "[engine]") {
    const auto input = makePhrase(2, kRate);
    PluginProcessor p;
    setParam(p, params::kThreshold, -50.f);
    setParam(p, params::kBypass, 1.f);
    const auto out = runProcessor(p, input, kRate, {512});
    CHECK(worstDifference(out, p.getLatencySamples(), input, 0, input.getNumSamples()) == 0.f);
}

TEST_CASE("the learner places the threshold under the typical ess", "[engine]") {
    // ess-like bursts at known levels, apart from each other
    const std::vector<float> levels_db{-30.f, -24.f, -27.f, -21.f, -18.f, -25.f, -22.f, -28.f, -20.f};
    const auto burst = static_cast<int>(kRate * .08), gap = static_cast<int>(kRate * .2);
    juce::AudioBuffer<float> input(1, static_cast<int>(levels_db.size()) * (burst + gap) + gap);
    input.clear();
    for (size_t i = 0; i < levels_db.size(); ++i) {
        addSine(input, 0, gap + static_cast<int>(i) * (burst + gap), burst, kRate, 7500.0,
                juce::Decibels::decibelsToGain(levels_db[i]));
    }
    dsp::Engine engine;
    engine.setFeedsActive(true);
    runEngine(engine, {}, input, kRate);

    dsp::ThresholdLearner learner;
    learner.start();
    float peak;
    int heard = 0;
    while (engine.pullEvent(peak)) {
        learner.add(peak);
        ++heard;
    }
    CHECK(heard == static_cast<int>(levels_db.size()));
    const auto threshold = learner.finish(-60.f, 0.f);
    REQUIRE(threshold.has_value());
    auto sorted = levels_db;
    std::sort(sorted.begin(), sorted.end());
    const auto expected = sorted[sorted.size() / 2] - dsp::ThresholdLearner::kTypicalReductionDb;
    CHECK(std::abs(*threshold - expected) < 1.5f);
}

TEST_CASE("parameter IDs never change", "[params]") {
    // saved sessions and presets refer to these; this list may only grow
    const juce::StringArray frozen{
        "threshold", "range", "mode", "processing", "shape", "band_low", "band_high", "lookahead", "stereo_link",
        "stereo_mode", "side_chain", "output_gain", "bypass"
    };
    PluginProcessor p;
    for (const auto& id : frozen) {
        CAPTURE(id);
        CHECK(p.parameters_.getParameter(id) != nullptr);
    }
    CHECK(params::allIDs() == frozen);
}

TEST_CASE("state round-trips, and listening is never saved", "[params]") {
    PluginProcessor a;
    setParam(a, params::kThreshold, -31.f);
    setParam(a, params::kBandLow, 4200.f);
    setParam(a, params::kBandHigh, 9000.f);
    setParam(a, params::kShape, 1.f);
    setParam(a, params::kStereoMode, 2.f);
    a.setListen(dsp::Listen::kDelta);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    PluginProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto& id : params::allIDs()) {
        CAPTURE(id);
        CHECK(std::abs(a.parameters_.getParameter(id)->getValue() - b.parameters_.getParameter(id)->getValue()) < 1e-6f);
    }
    CHECK(b.getListen() == dsp::Listen::kOff);
    CHECK_FALSE(state.toString().contains("listen"));
}

TEST_CASE("values read back from their text", "[params]") {
    PluginProcessor p;
    auto* high = p.parameters_.getParameter(params::kBandHigh);
    CHECK(high->getText(1.f, 32) == "Open");
    CHECK(std::abs(high->getValueForText("Open") - 1.f) < 1e-6f);
    CHECK(std::abs(high->convertFrom0to1(high->getValueForText("8 kHz")) - 8000.f) < 1.f);
    auto* low = p.parameters_.getParameter(params::kBandLow);
    const auto shown = low->getText(low->convertTo0to1(5500.f), 32);
    CHECK(shown == "5.50 kHz");
    CHECK(std::abs(low->convertFrom0to1(low->getValueForText(shown)) - 5500.f) < 1.f);
    auto* threshold = p.parameters_.getParameter(params::kThreshold);
    const auto t = threshold->getText(threshold->convertTo0to1(-24.f), 32);
    CHECK(t == juce::String::fromUTF8("\xe2\x88\x92") + "24.0 dB");
    CHECK(std::abs(threshold->convertFrom0to1(threshold->getValueForText(t)) + 24.f) < .05f);
}

TEST_CASE("a band keeps its edges apart, the edge being set winning", "[params]") {
    // the high edge set below the low one pulls the low edge down
    auto [low, high] = params::constrainBand(5500.f, 2000.f, false);
    CHECK(high == 2000.f);
    CHECK(std::abs(low - 2000.f / params::kMinBandRatio) < .5f);
    // the low edge set above the high one pushes the high edge up
    std::tie(low, high) = params::constrainBand(9000.f, 6000.f, true);
    CHECK(low == 9000.f);
    CHECK(std::abs(high - 9000.f * params::kMinBandRatio) < .5f);
    // an open band only has a low edge
    std::tie(low, high) = params::constrainBand(15000.f, params::kBandHighMax, true);
    CHECK(low == 15000.f);
    CHECK(params::isOpen(high));
    // at the ends of the ranges the other edge stops, and they stay apart
    std::tie(low, high) = params::constrainBand(16000.f, 12000.f, true);
    CHECK_FALSE(params::isOpen(high));
    CHECK(high >= low * params::kMinBandRatio - .5f);
    // the high edge stops at the bottom of its own range first
    std::tie(low, high) = params::constrainBand(4000.f, 1000.f, false);
    CHECK(high == params::kBandHighMin);
    CHECK(std::abs(low - params::kBandHighMin / params::kMinBandRatio) < .5f);
    CHECK(high >= low * params::kMinBandRatio - .5f);
}

TEST_CASE("the drawn response is what the engine does", "[engine]") {
    struct Case {
        double rate;
        dsp::Shape shape;
        float low, high;
        bool open;
    };
    const auto c_ = GENERATE(Case{48000.0, dsp::Shape::kShelf, 5500.f, 20000.f, true},
                             Case{48000.0, dsp::Shape::kBell, 10000.f, 16000.f, false},
                             Case{44100.0, dsp::Shape::kBell, 5000.f, 8000.f, false},
                             Case{96000.0, dsp::Shape::kShelf, 12000.f, 20000.f, true});
    CAPTURE(c_.rate, static_cast<int>(c_.shape), c_.low, c_.high);
    // a loud side chain holds the reduction at the range
    dsp::Controls c;
    c.mode = dsp::Mode::kAllround;
    c.threshold_db = -60.f;
    c.range_db = 12.f;
    c.shape = c_.shape;
    c.band_low = c_.low;
    c.band_high = c_.high;
    c.band_open = c_.open;
    const auto len = static_cast<int>(c_.rate * .4);
    juce::AudioBuffer<float> side(1, len);
    side.clear();
    addEss(side, 0, 0, len, c_.rate, .3f, 9000.0);
    addSine(side, 0, 0, len, c_.rate, 11000.0, .2f);
    const auto latency = dsp::Engine::lookaheadSamples(c.lookahead_ms, c_.rate);
    const auto start = len / 3, length = len / 2;
    for (const auto hz : {1000.0, 4000.0, 7000.0, 9000.0, 12000.0, 15000.0, 18000.0}) {
        if (hz > .45 * c_.rate) {
            continue;
        }
        CAPTURE(hz);
        juce::AudioBuffer<float> input(1, len);
        input.clear();
        addSine(input, 0, 0, len, c_.rate, hz, .1f);
        dsp::Engine engine;
        const auto out = runEngine(engine, c, input, c_.rate, &side);
        const auto measured = toneDb(out, 0, start + latency, length, c_.rate, hz) - toneDb(input, 0, start, length, c_.rate, hz);
        CHECK(std::abs(measured - dsp::Engine::responseDb(c, c_.rate, static_cast<float>(hz), 12.f)) < .25f);
    }
}

TEST_CASE("look-ahead, which sets the latency, is not automatable", "[params]") {
    PluginProcessor p;
    CHECK_FALSE(p.parameters_.getParameter(params::kLookahead)->isAutomatable());
    CHECK(p.parameters_.getParameter(params::kThreshold)->isAutomatable());
}

TEST_CASE("the one program has a name", "[params]") {
    PluginProcessor p;
    CHECK(p.getNumPrograms() == 1);
    CHECK(p.getProgramName(0).isNotEmpty());
}
