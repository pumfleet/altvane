// Copyright (C) 2026 - Altvane, based on ZLLimiter by zsliu98
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "test_helpers.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;
using altvane::limiter::LoudnessMeter;
using altvane::limiter::LoudnessReadings;

namespace {
    /**
     * a stereo 1 kHz sine programme: {seconds, peak dBFS} segments, as in EBU Tech 3341 and 3342
     */
    LoudnessReadings measure(const std::vector<std::pair<double, float>>& programme, const int block = 512) {
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 2, static_cast<size_t>(block));
        std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
        int64_t n = 0;
        for (const auto& [seconds, db] : programme) {
            const auto amp = juce::Decibels::decibelsToGain(static_cast<double>(db), -1000.0);
            const auto end = n + static_cast<int64_t>(seconds * kSampleRate);
            while (n < end) {
                const auto count = static_cast<size_t>(std::min<int64_t>(block, end - n));
                for (size_t i = 0; i < count; ++i) {
                    const auto v = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * 1000.0 *
                                                                     static_cast<double>(n + static_cast<int64_t>(i)) / kSampleRate));
                    l[i] = v;
                    r[i] = v;
                }
                const std::array<const float*, 2> channels{l.data(), r.data()};
                meter.process(channels, count);
                n += static_cast<int64_t>(count);
            }
        }
        return meter.pull();
    }
}

TEST_CASE("EBU Tech 3341: momentary, short-term and integrated loudness of steady sines", "[loudness]") {
    for (const auto db : {-23.f, -33.f}) {
        const auto r = measure({{20.0, db}});
        INFO(db << " dBFS");
        CHECK_THAT(r.momentary, WithinAbs(db, .1));
        CHECK_THAT(r.short_term, WithinAbs(db, .1));
        REQUIRE(r.has_integrated);
        CHECK_THAT(r.integrated, WithinAbs(db, .1));
    }
}

TEST_CASE("EBU Tech 3341: integrated loudness gates quiet passages", "[loudness]") {
    // case 3: the relative gate removes the -36 dBFS parts
    CHECK_THAT(measure({{10.0, -36.f}, {60.0, -23.f}, {10.0, -36.f}}).integrated, WithinAbs(-23.0, .1));
    // case 4: the absolute gate removes the -72 dBFS parts too
    CHECK_THAT(measure({{10.0, -72.f}, {10.0, -36.f}, {60.0, -23.f}, {10.0, -36.f}, {10.0, -72.f}}).integrated,
               WithinAbs(-23.0, .1));
}

TEST_CASE("EBU Tech 3342: loudness range", "[loudness]") {
    CHECK_THAT(measure({{20.0, -20.f}, {20.0, -30.f}}).range, WithinAbs(10.0, 1.0));
    CHECK_THAT(measure({{20.0, -20.f}, {20.0, -15.f}}).range, WithinAbs(5.0, 1.0));
    CHECK_THAT(measure({{20.0, -40.f}, {20.0, -20.f}}).range, WithinAbs(20.0, 1.0));
    CHECK_THAT(measure({{20.0, -50.f}, {20.0, -35.f}, {20.0, -20.f}, {20.0, -35.f}, {20.0, -50.f}}).range,
               WithinAbs(15.0, 1.0));
}

TEST_CASE("the meter does not depend on the block size", "[loudness]") {
    const auto a = measure({{5.0, -18.f}}, 64);
    const auto b = measure({{5.0, -18.f}}, 4096);
    CHECK_THAT(a.integrated, WithinAbs(b.integrated, 1e-3));
    CHECK_THAT(a.short_term, WithinAbs(b.short_term, 1e-3));
}

TEST_CASE("maxima, true peak and the short-term histogram", "[loudness]") {
    const auto r = measure({{5.0, -30.f}, {5.0, -18.f}, {5.0, -30.f}});
    CHECK_THAT(r.max_momentary, WithinAbs(-18.0, .1));
    CHECK_THAT(r.max_short_term, WithinAbs(-18.0, .1));
    CHECK_THAT(r.true_peak_max, WithinAbs(-18.0, .05));
    CHECK_THAT(r.seconds, WithinAbs(15.0, .2));
    float total = 0.f;
    for (const auto v : r.histogram) {
        total += v;
    }
    CHECK(total > 100.f);
    CHECK(r.histogram[static_cast<size_t>(-18 - LoudnessReadings::kHistogramLow - 1)] +
          r.histogram[static_cast<size_t>(-18 - LoudnessReadings::kHistogramLow)] > 0.f);
}

namespace {
    struct FakePlayHead final : juce::AudioPlayHead {
        bool playing{false};
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setIsPlaying(playing);
            return info;
        }
    };
}

TEST_CASE("the processor measures the output and starts over when playback starts", "[loudness]") {
    PluginProcessor p;
    FakePlayHead playhead;
    p.setPlayHead(&playhead);
    prepare(p);

    // stopped: nothing counts towards the integrated loudness
    run(p, sine(1000.0, -23.f), 2.0);
    auto r = p.pullLoudness();
    CHECK_FALSE(r.has_integrated);
    CHECK_THAT(r.momentary, WithinAbs(-23.0, .2));

    playhead.playing = true;
    run(p, sine(1000.0, -23.f), 5.0);
    r = p.pullLoudness();
    const auto generation = r.generation;
    REQUIRE(r.has_integrated);
    CHECK_THAT(r.integrated, WithinAbs(-23.0, .1));
    CHECK_THAT(r.seconds, WithinAbs(5.0, .5));

    // stop, then play again: a fresh measurement
    playhead.playing = false;
    run(p, sine(1000.0, -23.f), .5);
    playhead.playing = true;
    run(p, sine(1000.0, -23.f), 1.0);
    r = p.pullLoudness();
    CHECK(r.generation > generation);
    CHECK(r.seconds < 1.5);

    // unless auto reset is off
    set(p.na_parameters_, zlstate::PLoudnessAutoReset::kID, 0.f);
    playhead.playing = false;
    run(p, sine(1000.0, -23.f), .5);
    playhead.playing = true;
    run(p, sine(1000.0, -23.f), 1.0);
    CHECK(p.pullLoudness().generation == r.generation);

    // and a reset can be asked for any time
    p.resetLoudness();
    run(p, sine(1000.0, -23.f), .2);
    CHECK(p.pullLoudness().generation > r.generation);
    p.setPlayHead(nullptr);
}

TEST_CASE("audio heard while stopped never reaches the integrated loudness", "[loudness]") {
    PluginProcessor p;
    FakePlayHead playhead;
    p.setPlayHead(&playhead);
    set(p.na_parameters_, zlstate::PLoudnessAutoReset::kID, 0.f);
    prepare(p);

    // stop, play, stop with something loud going through, then play on: with auto reset off the measurement
    // continues, and the loud stopped audio must not leak into it through the momentary or short-term windows
    playhead.playing = true;
    run(p, sine(1000.0, -30.f), 4.0);
    playhead.playing = false;
    run(p, sine(1000.0, -10.f), 3.05);
    playhead.playing = true;
    run(p, sine(1000.0, -30.f), 4.0);
    const auto r = p.pullLoudness();
    REQUIRE(r.has_integrated);
    CHECK_THAT(r.integrated, WithinAbs(-30.0, .1));
    CHECK_THAT(r.max_momentary, WithinAbs(-30.0, .1));
    CHECK_THAT(r.max_short_term, WithinAbs(-30.0, .1));
    CHECK(r.seconds < 8.1);
    p.setPlayHead(nullptr);
}
