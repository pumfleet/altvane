// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "alloc_counter.hpp"
#include "signals.hpp"

using namespace test_signals;

namespace {
    // a level change the rider can follow fully: target -20, range +-12
    void defaults(PluginProcessor& p) {
        setParam(p, params::kTarget, -20.f);
        setParam(p, params::kRangeMin, -12.f);
        setParam(p, params::kRangeMax, 12.f);
        setParam(p, params::kIdle, 0.f);
        setParam(p, params::kSpeed, .5f);
        setParam(p, params::kGuard, .7f);
    }

    float worstDifference(const juce::AudioBuffer<float>& a, const int a_offset, const juce::AudioBuffer<float>& b,
                          const int length) {
        float worst = 0.f;
        for (int c = 0; c < b.getNumChannels(); ++c) {
            for (int i = 0; i < length; ++i) {
                worst = std::max(worst, std::abs(a.getSample(c, i + a_offset) - b.getSample(c, i)));
            }
        }
        return juce::Decibels::gainToDecibels(worst, -200.f);
    }
}

TEST_CASE("a steady voice is ridden onto the target", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0, 192000.0);
    const auto channels = GENERATE(1, 2);
    const auto level = GENERATE(-29.f, -13.f);
    CAPTURE(rate, channels, level);
    PluginProcessor p;
    defaults(p);
    juce::AudioBuffer<float> input(channels, samples(rate, 4.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), level, false);
    const auto r = run(p, input, rate);
    // the voice as the meter sees it, plus the gain, lands on the target
    const auto& c = columnAt(r, rate, 3.8);
    CHECK(c.state == dsp::HopState::kVoiced);
    CHECK(std::abs(c.in_db + c.gain_db - (-20.f)) < .5f);
    CHECK(std::abs(c.gain_db - (-20.f - level)) < .5f);
    // and the output really is at the target
    CHECK(std::abs(loudness(r.output, rate, samples(rate, 3.0) + r.latency, samples(rate, 3.9) + r.latency) - (-20.f)) < .5f);
}

TEST_CASE("a stereo voice with its channels out of polarity is still a voice", "[engine]") {
    PluginProcessor p;
    defaults(p);
    // no riding at all on anything that is not a voice
    setParam(p, params::kGuard, 1.f);
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(2, samples(rate, 4.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -29.f, false);
    input.applyGain(1, 0, input.getNumSamples(), -1.f);
    const auto r = run(p, input, rate);
    const auto& c = columnAt(r, rate, 3.8);
    CHECK(c.state == dsp::HopState::kVoiced);
    CHECK(std::abs(c.gain_db - 9.f) < .5f);
}

TEST_CASE("the gain stays inside the range", "[engine]") {
    PluginProcessor p;
    defaults(p);
    setParam(p, params::kRangeMin, -3.f);
    setParam(p, params::kRangeMax, 4.f);
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(1, samples(rate, 6.0));
    input.clear();
    addVoice(input, rate, 0, samples(rate, 3.0), -40.f, false);
    addVoice(input, rate, samples(rate, 3.0), samples(rate, 6.0), -5.f, false);
    const auto r = run(p, input, rate);
    CHECK(std::abs(columnAt(r, rate, 2.8).gain_db - 4.f) < .05f);
    CHECK(std::abs(columnAt(r, rate, 5.8).gain_db + 3.f) < .05f);
    const auto [lo, hi] = gainRange(r, rate, 0.0, 5.9);
    CHECK(lo >= -3.001f);
    CHECK(hi <= 4.001f);
}

TEST_CASE("gaps between syllables hold the gain; a long silence returns it to idle", "[engine]") {
    PluginProcessor p;
    defaults(p);
    setParam(p, params::kIdle, -4.f);
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(1, samples(rate, 8.0));
    input.clear();
    addVoice(input, rate, 0, samples(rate, 4.5), -28.f, true);
    const auto r = run(p, input, rate);
    // across the 100 ms gaps of the phrase, the gain barely moves
    const auto [lo, hi] = gainRange(r, rate, 3.0, 4.4);
    CHECK(hi - lo < 1.5f);
    CHECK(lo > 6.f);
    // just after the phrase it still holds, then settles on idle
    CHECK(columnAt(r, rate, 4.6).gain_db > 6.f);
    CHECK(std::abs(columnAt(r, rate, 7.9).gain_db + 4.f) < .5f);
}

TEST_CASE("breaths and sibilants do not move the gain", "[engine]") {
    const auto sibilant = GENERATE(false, true);
    CAPTURE(sibilant);
    constexpr double rate = 48000.0;
    // a phrase at the target, interrupted by a breath (well below the voice) or a sibilant (well above it)
    juce::AudioBuffer<float> input(1, samples(rate, 4.0));
    input.clear();
    addVoice(input, rate, 0, samples(rate, 2.5), -20.f, false);
    addNoise(input, rate, samples(rate, 2.5), samples(rate, 2.9), sibilant ? -10.f : -33.f, sibilant);
    addVoice(input, rate, samples(rate, 2.9), samples(rate, 4.0), -20.f, false);

    auto moved = [&](const float guard) {
        PluginProcessor p;
        defaults(p);
        setParam(p, params::kGuard, guard);
        const auto r = run(p, input, rate);
        const auto before = columnAt(r, rate, 2.45).gain_db;
        const auto [lo, hi] = gainRange(r, rate, 2.5, 2.95);
        return std::max(hi - before, before - lo);
    };
    const auto guarded = moved(.7f);
    const auto unguarded = moved(0.f);
    CAPTURE(guarded, unguarded);
    CHECK(guarded < 1.5f);
    CHECK(unguarded > 4.f);
}

TEST_CASE("the target follows the music key, mono or stereo", "[engine]") {
    const auto key_channels = GENERATE(1, 2);
    CAPTURE(key_channels);
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(2, samples(rate, 12.0)), key(key_channels, samples(rate, 12.0));
    input.clear();
    key.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -24.f, false);
    // the chorus comes in 6 dB louder
    addMusic(key, rate, 0, samples(rate, 8.0), -26.f);
    addMusic(key, rate, samples(rate, 8.0), key.getNumSamples(), -20.f);

    auto lift = [&](const float music) {
        PluginProcessor p;
        defaults(p);
        setParam(p, params::kMusic, music);
        const auto r = run(p, input, rate, {512}, &key);
        return columnAt(r, rate, 10.0).target_db - columnAt(r, rate, 7.5).target_db;
    };
    const auto full = lift(1.f);
    const auto half = lift(.5f);
    CAPTURE(full, half);
    // the long-term memory of the key moves a little in the 2 s since the step
    CHECK(full > 5.f);
    CHECK(full < 6.f);
    CHECK(std::abs(half - full * .5f) < .1f);
    CHECK(std::abs(lift(0.f)) < .01f);
}

TEST_CASE("speed runs from 1.5 s to 40 ms, and cuts are twice as fast as boosts", "[engine]") {
    CHECK(std::abs(dsp::Rider::boostTime(0.f) - 1.5) < 1e-9);
    CHECK(std::abs(dsp::Rider::boostTime(1.f) - .04) < 1e-9);
    for (int i = 0; i < 10; ++i) {
        CHECK(dsp::Rider::boostTime(static_cast<float>(i + 1) * .1f) < dsp::Rider::boostTime(static_cast<float>(i) * .1f));
    }
    // the time the rider takes to cover 63 % of a level change (one time constant), hop by hop
    constexpr double kHop = .005;
    const auto timeConstant = [](const float speed, const float change_db) {
        dsp::Rider rider;
        rider.prepare(kHop);
        rider.reset(0.f);
        dsp::Rider::Settings s;
        s.speed = speed;
        s.target_db = -20.f;
        s.range_min = -12.f;
        s.range_max = 12.f;
        // a phrase at the target, long enough for the phrase-start catch-up to be over
        for (int i = 0; i < 200; ++i) {
            rider.update(dsp::HopState::kVoiced, -20.f, -20.f, s);
        }
        REQUIRE(std::abs(rider.getGain()) < 1e-3f);
        // then the voice moves: the gain has to follow by change_db
        const auto level = -20.f - change_db;
        int hops = 0;
        while (std::abs(rider.getGain()) < std::abs(change_db) * (1.f - std::exp(-1.f)) && hops < 100000) {
            rider.update(dsp::HopState::kVoiced, level, level, s);
            ++hops;
        }
        return hops * kHop;
    };
    for (const auto speed : {0.f, .5f, 1.f}) {
        CAPTURE(speed);
        const auto boost = timeConstant(speed, 6.f);
        const auto cut = timeConstant(speed, -6.f);
        CAPTURE(boost, cut);
        CHECK(std::abs(boost - dsp::Rider::boostTime(speed)) <= kHop * 1.01);
        CHECK(std::abs(cut - dsp::Rider::boostTime(speed) * .5) <= kHop * 1.01);
    }
}

TEST_CASE("the speed control reaches the rider", "[engine]") {
    constexpr double rate = 48000.0;
    // a phrase that drops 10 dB: how far the gain has followed 200 ms later
    juce::AudioBuffer<float> input(1, samples(rate, 4.0));
    input.clear();
    addVoice(input, rate, 0, samples(rate, 2.0), -20.f, false);
    addVoice(input, rate, samples(rate, 2.0), samples(rate, 4.0), -30.f, false);
    auto followed = [&](const float speed) {
        PluginProcessor p;
        defaults(p);
        setParam(p, params::kSpeed, speed);
        const auto r = run(p, input, rate);
        return columnAt(r, rate, 2.2).gain_db - columnAt(r, rate, 1.9).gain_db;
    };
    const auto slow = followed(0.f);
    const auto fast = followed(1.f);
    CAPTURE(slow, fast);
    CHECK(fast > 9.f);
    CHECK(slow < 3.f);
}

TEST_CASE("the reported latency is the lookahead and the delay of an impulse", "[engine]") {
    const auto lookahead = GENERATE(false, true);
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    CAPTURE(lookahead, rate);
    PluginProcessor p;
    // no riding at all
    setParam(p, params::kRangeMin, 0.f);
    setParam(p, params::kRangeMax, 0.f);
    setParam(p, params::kLookahead, lookahead ? 1.f : 0.f);
    juce::AudioBuffer<float> input(1, samples(rate, .5));
    input.clear();
    input.setSample(0, 1000, 1.f);
    const auto r = run(p, input, rate, {480});
    CHECK(r.latency == (lookahead ? dsp::kLookaheadHops * r.hop : 0));
    CHECK(std::abs(r.latency / rate - (lookahead ? .03 : 0.0)) < .0002);
    int peak = 0;
    for (int i = 0; i < r.output.getNumSamples(); ++i) {
        if (std::abs(r.output.getSample(0, i)) > std::abs(r.output.getSample(0, peak))) {
            peak = i;
        }
    }
    CHECK(peak == 1000 + r.latency);
}

TEST_CASE("with lookahead the gain is in place when a phrase starts", "[engine]") {
    constexpr double rate = 48000.0;
    // a quiet phrase after a long silence needs +10 dB
    juce::AudioBuffer<float> input(1, samples(rate, 4.0));
    input.clear();
    addVoice(input, rate, samples(rate, 2.0), samples(rate, 4.0), -30.f, false);

    auto onsetError = [&](const bool lookahead) {
        PluginProcessor p;
        defaults(p);
        setParam(p, params::kLookahead, lookahead ? 1.f : 0.f);
        const auto r = run(p, input, rate);
        const auto settled = columnAt(r, rate, 3.9).gain_db;
        // catching up never overshoots
        CHECK(gainRange(r, rate, 2.0, 2.5).second < settled + 1.f);
        // the gain applied to the first 10 ms of the phrase
        const auto [lo, hi] = gainRange(r, rate, 2.0, 2.01);
        return settled - lo;
    };
    const auto with = onsetError(true);
    const auto without = onsetError(false);
    CAPTURE(with, without);
    CHECK(with < 3.f);
    CHECK(without > with + 3.f);
}

TEST_CASE("with nothing to ride the output is the delayed input", "[engine]") {
    const auto channels = GENERATE(1, 2);
    const auto bypass = GENERATE(false, true);
    CAPTURE(channels, bypass);
    PluginProcessor p;
    if (bypass) {
        defaults(p);
        setParam(p, params::kBypass, 1.f);
    } else {
        setParam(p, params::kRangeMin, 0.f);
        setParam(p, params::kRangeMax, 0.f);
    }
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(channels, samples(rate, 2.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -30.f, true);
    const auto r = run(p, input, rate, {512, 7, 300, 1, 64, 333});
    CHECK(worstDifference(r.output, r.latency, input, input.getNumSamples()) < -90.f);
}

TEST_CASE("in bypass the history shows the output as heard", "[engine]") {
    PluginProcessor p;
    defaults(p);
    setParam(p, params::kBypass, 1.f);
    setParam(p, params::kOutput, 6.f);
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(1, samples(rate, 3.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -32.f, false);
    const auto r = run(p, input, rate);
    const auto& c = columnAt(r, rate, 2.8);
    // the rider still rides (for when bypass is lifted), but the output is the input
    CHECK(c.gain_db > 5.f);
    CHECK(std::abs(c.out_db - c.in_db) < .01f);
}

TEST_CASE("offline output does not depend on the block size", "[engine]") {
    PluginProcessor p;
    defaults(p);
    constexpr double rate = 44100.0;
    juce::AudioBuffer<float> input(2, samples(rate, 3.0));
    input.clear();
    addVoice(input, rate, 0, samples(rate, 1.5), -30.f, true);
    addVoice(input, rate, samples(rate, 1.5), input.getNumSamples(), -12.f, true);
    const auto a = run(p, input, rate, {512});
    const auto b = run(p, input, rate, {512, 7, 300, 1, 64, 333});
    CHECK(worstDifference(a.output, 0, b.output, b.output.getNumSamples()) < -120.f);
}

TEST_CASE("read mode applies the rider fader", "[engine]") {
    PluginProcessor p;
    defaults(p);
    p.setMode(dsp::Mode::kRead);
    *p.getRide() = -7.5f;
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(1, samples(rate, 1.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -40.f, false);
    const auto r = run(p, input, rate);
    juce::AudioBuffer<float> expected(1, input.getNumSamples());
    expected.copyFrom(0, 0, input, 0, 0, input.getNumSamples());
    expected.applyGain(juce::Decibels::decibelsToGain(-7.5f));
    CHECK(worstDifference(r.output, r.latency, expected, input.getNumSamples()) < -90.f);
}

TEST_CASE("the audio thread does not allocate", "[engine]") {
    PluginProcessor p;
    defaults(p);
    setParam(p, params::kMusic, .5f);
    constexpr double rate = 48000.0;
    constexpr int kBlock = 256;
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
    REQUIRE(p.setBusesLayout(layout));
    p.setRateAndBufferSizeDetails(rate, kBlock);
    p.prepareToPlay(rate, kBlock);
    p.getEngine().setPublishing(true);
    juce::AudioBuffer<float> input(2, samples(rate, 2.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -25.f, true);
    juce::AudioBuffer<float> io(4, kBlock);
    juce::MidiBuffer midi;
    size_t allocations = 0;
    for (int pos = 0; pos + kBlock <= input.getNumSamples(); pos += kBlock) {
        for (int c = 0; c < 4; ++c) {
            io.copyFrom(c, 0, input, c % 2, pos, kBlock);
        }
        alloc_counter::Scope scope;
        p.processBlock(io, midi);
        allocations += scope.count();
    }
    CHECK(allocations == 0);
    p.releaseResources();
}
