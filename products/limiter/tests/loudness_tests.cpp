// Copyright (C) 2026 - Altvane, based on ZLLimiter by zsliu98
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

/**
 * the loudness meter against the minimum-requirements test signals of EBU Tech 3341 (2023) and Tech 3342 (2023),
 * synthesised here from the descriptions in their Table 1 (the EBU's own test files are not used). Cases 6 (5.0
 * channels), 7 and 8 of Tech 3341 and 5 and 6 of Tech 3342 (authentic programme) are left out: the meter is mono or
 * stereo, and the programme files are not ours to ship.
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "test_helpers.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;
using altvane::limiter::LoudnessMeter;
using altvane::limiter::LoudnessReadings;

namespace {
    // a level for silence in a programme
    constexpr float kSilence = -1000.f;

    /**
     * a stereo programme as in Tech 3341 and 3342: {seconds, per-channel peak dBFS} segments of a 1 kHz sine, applied
     * in phase to both channels, the phase running on across segments
     */
    using Programme = std::vector<std::pair<double, float>>;

    class SineMeter {
    public:
        explicit SineMeter(const double rate = kSampleRate, const int block = 512) :
            rate_(rate), l_(static_cast<size_t>(block)), r_(static_cast<size_t>(block)) {
            meter_.prepare(rate, 2, static_cast<size_t>(block));
        }

        /**
         * feed a programme; `each_step` sees the readings after every 100 ms of it, with the time since the start
         */
        void feed(const Programme& programme,
                  const std::function<void(double, const LoudnessReadings&)>& each_step = {}) {
            const auto step = static_cast<int64_t>(std::lround(rate_ * .1));
            for (const auto& [seconds, db] : programme) {
                const auto amp = juce::Decibels::decibelsToGain(static_cast<double>(db), -1000.0);
                const auto end = n_ + static_cast<int64_t>(std::lround(seconds * rate_));
                while (n_ < end) {
                    // stop at the next 100 ms mark, so the readings can be taken there
                    const auto next_step = (n_ / step + 1) * step;
                    const auto count = static_cast<size_t>(std::min<int64_t>({static_cast<int64_t>(l_.size()),
                                                                               end - n_, next_step - n_}));
                    for (size_t i = 0; i < count; ++i) {
                        const auto t = static_cast<double>(n_ + static_cast<int64_t>(i)) / rate_;
                        const auto v = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * 1000.0 * t));
                        l_[i] = v;
                        r_[i] = v;
                    }
                    const std::array<const float*, 2> channels{l_.data(), r_.data()};
                    meter_.process(channels, count);
                    n_ += static_cast<int64_t>(count);
                    if (each_step && n_ % step == 0) {
                        each_step(static_cast<double>(n_) / rate_, meter_.pull());
                    }
                }
            }
        }

        const LoudnessReadings& pull() { return meter_.pull(); }

    private:
        double rate_;
        LoudnessMeter meter_;
        std::vector<float> l_, r_;
        int64_t n_{0};
    };

    LoudnessReadings measure(const Programme& programme, const double rate = kSampleRate, const int block = 512) {
        SineMeter meter(rate, block);
        meter.feed(programme);
        return meter.pull();
    }

    Programme repeat(const Programme& p, const int times) {
        Programme result;
        for (int i = 0; i < times; ++i) {
            result.insert(result.end(), p.begin(), p.end());
        }
        return result;
    }
}

TEST_CASE("EBU Tech 3341 cases 1 and 2: momentary, short-term and integrated loudness of steady sines", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    for (const auto db : {-23.f, -33.f}) {
        const auto r = measure({{20.0, db}}, rate);
        INFO(rate << " Hz, " << db << " dBFS");
        CHECK_THAT(r.momentary, WithinAbs(db, .1));
        CHECK_THAT(r.short_term, WithinAbs(db, .1));
        REQUIRE(r.has_integrated);
        CHECK_THAT(r.integrated, WithinAbs(db, .1));
    }
}

TEST_CASE("EBU Tech 3341 cases 3 to 5: integrated loudness gates quiet passages", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    CAPTURE(rate);
    // case 3: the relative gate removes the -36 dBFS parts
    CHECK_THAT(measure({{10.0, -36.f}, {60.0, -23.f}, {10.0, -36.f}}, rate).integrated, WithinAbs(-23.0, .1));
    // case 4: the absolute gate removes the -72 dBFS parts too
    CHECK_THAT(measure({{10.0, -72.f}, {10.0, -36.f}, {60.0, -23.f}, {10.0, -36.f}, {10.0, -72.f}}, rate).integrated,
               WithinAbs(-23.0, .1));
    // case 5: nothing is gated, the louder middle lifts the whole to -23
    CHECK_THAT(measure({{20.0, -26.f}, {20.1, -20.f}, {20.0, -26.f}}, rate).integrated, WithinAbs(-23.0, .1));
}

TEST_CASE("EBU Tech 3341 case 9: short-term loudness of a 3 s pattern is constant after 3 s", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    CAPTURE(rate);
    SineMeter meter(rate);
    int readings = 0;
    meter.feed(repeat({{1.34, -20.f}, {1.66, -30.f}}, 5), [&](const double t, const LoudnessReadings& r) {
        if (t >= 3.0 - 1e-9) {
            CAPTURE(t);
            CHECK_THAT(r.short_term, WithinAbs(-23.0, .1));
            ++readings;
        }
    });
    CHECK(readings == 121);
}

TEST_CASE("EBU Tech 3341 case 12: momentary loudness of a 400 ms pattern is constant after 1 s", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    CAPTURE(rate);
    SineMeter meter(rate);
    int readings = 0;
    meter.feed(repeat({{.18, -20.f}, {.22, -30.f}}, 25), [&](const double t, const LoudnessReadings& r) {
        if (t >= 1.0 - 1e-9) {
            CAPTURE(t);
            CHECK_THAT(r.momentary, WithinAbs(-23.0, .1));
            ++readings;
        }
    });
    CHECK(readings == 91);
}

TEST_CASE("EBU Tech 3341 cases 10 and 13: maximum short-term and momentary loudness of a burst, however it falls",
          "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0);
    CAPTURE(rate);
    // 20 separate measurements each; a burst after i * 150 ms (case 10) or i * 20 ms (case 13) of silence, so it does
    // not line up with the meter's 100 ms updates
    for (int i = 0; i < 20; ++i) {
        CAPTURE(i);
        CHECK_THAT(measure({{i * .15, kSilence}, {3.0, -23.f}, {1.0, kSilence}}, rate).max_short_term,
                   WithinAbs(-23.0, .1));
        CHECK_THAT(measure({{i * .02, kSilence}, {.4, -23.f}, {1.0, kSilence}}, rate).max_momentary,
                   WithinAbs(-23.0, .1));
    }
}

TEST_CASE("EBU Tech 3341 cases 11 and 14: maximum short-term and momentary loudness, live", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0);
    CAPTURE(rate);
    // one measurement of 20 bursts, each 1 dB louder than the one before: the maximum after each is its level
    SineMeter short_term(rate), momentary(rate);
    for (int i = 0; i < 20; ++i) {
        CAPTURE(i);
        const auto db = -38.f + static_cast<float>(i);
        short_term.feed({{i * .15, kSilence}, {3.0, db}, {3.0 - i * .15, kSilence}});
        CHECK_THAT(short_term.pull().max_short_term, WithinAbs(db, .1));
        momentary.feed({{i * .02, kSilence}, {.4, db}, {.4 - i * .02, kSilence}});
        CHECK_THAT(momentary.pull().max_momentary, WithinAbs(db, .1));
    }
}

namespace {
    /**
     * a stereo tone (both channels) with 10 ms fades at the ends, as the true-peak cases taper it
     */
    std::vector<float> taperedSine(const double rate, const double hz, const double amplitude, const double phase_deg,
                                   const double seconds = 1.0) {
        const auto length = static_cast<size_t>(seconds * rate);
        const auto fade = rate * .01;
        std::vector<float> x(length);
        for (size_t n = 0; n < length; ++n) {
            const auto taper = std::min({1.0, static_cast<double>(n) / fade, static_cast<double>(length - 1 - n) / fade});
            x[n] = static_cast<float>(taper * amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) /
                                                                   rate + phase_deg * std::numbers::pi / 180.0));
        }
        return x;
    }

    /**
     * cases 20 to 23: an fs/6 sine of amplitude 0.5 with one period of an fs/4 sine of amplitude 1 in it, continuous in
     * phase at both ends, made at 4 fs, low-passed and taken down to fs starting at `offset` (at 4 fs)
     */
    std::vector<float> burstInSine(const double rate, const int offset) {
        constexpr int kOver = 4;
        // at 4 fs an fs/6 period is 24 samples and an fs/4 period 16
        constexpr int kSlow = 24, kFast = 16;
        const auto lead = static_cast<int>(rate * .5) / kSlow * kSlow * kOver;
        const auto length = 2 * lead + kFast;
        std::vector<double> high(static_cast<size_t>(length));
        for (int n = 0; n < length; ++n) {
            double v;
            if (n < lead) {
                v = .5 * std::sin(2.0 * std::numbers::pi * n / kSlow);
            } else if (n < lead + kFast) {
                v = std::sin(2.0 * std::numbers::pi * (n - lead) / kFast);
            } else {
                v = .5 * std::sin(2.0 * std::numbers::pi * (n - lead - kFast) / kSlow);
            }
            // a short fade at the ends
            const auto fade = std::min({1.0, n / (rate * kOver * .01), (length - 1 - n) / (rate * kOver * .01)});
            high[static_cast<size_t>(n)] = v * fade;
        }
        // anti-aliasing: a Kaiser-windowed sinc low-pass at 0.45 fs (0.1125 of 4 fs)
        constexpr int kHalf = 256;
        constexpr double kCutoff = .45 / kOver;
        const auto bessel = [](const double v) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k) {
                term *= (v / (2.0 * k)) * (v / (2.0 * k));
                sum += term;
            }
            return sum;
        };
        std::vector<double> h;
        for (int k = -kHalf; k <= kHalf; ++k) {
            const auto sinc = k == 0 ? 2.0 * kCutoff : std::sin(2.0 * std::numbers::pi * kCutoff * k) / (std::numbers::pi * k);
            const auto r = static_cast<double>(k) / kHalf;
            h.push_back(sinc * bessel(9.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel(9.0));
        }
        std::vector<float> low;
        for (int n = offset; n < length; n += kOver) {
            double sum = 0.0;
            for (int k = -kHalf; k <= kHalf; ++k) {
                const auto m = n - k;
                if (m >= 0 && m < length) {
                    sum += h[static_cast<size_t>(k + kHalf)] * high[static_cast<size_t>(m)];
                }
            }
            low.push_back(static_cast<float>(sum));
        }
        return low;
    }

    float meterTruePeak(const std::vector<float>& x, const double rate) {
        LoudnessMeter meter;
        meter.prepare(rate, 2, 512);
        for (size_t start = 0; start < x.size(); start += 512) {
            const std::array<const float*, 2> channels{x.data() + start, x.data() + start};
            meter.process(channels, std::min<size_t>(512, x.size() - start));
        }
        return meter.pull().true_peak_max;
    }
}

TEST_CASE("EBU Tech 3341 cases 15 to 19: true peak of sines between the samples", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    struct Case {
        int number;
        double divisor, amplitude, phase, expected;
    };
    for (const auto c : {Case{15, 4.0, .5, 0.0, -6.0}, Case{16, 4.0, .5, 45.0, -6.0}, Case{17, 6.0, .5, 60.0, -6.0},
                         Case{18, 8.0, .5, 67.5, -6.0}, Case{19, 4.0, 1.41, 45.0, 3.0}}) {
        CAPTURE(rate, c.number);
        const auto peak = meterTruePeak(taperedSine(rate, rate / c.divisor, c.amplitude, c.phase), rate);
        // the accepted tolerance is +0.2 / -0.4 dB
        CHECK(peak <= c.expected + .2);
        CHECK(peak >= c.expected - .4);
    }
}

TEST_CASE("EBU Tech 3341 cases 20 to 23: true peak of a burst, at four sampling offsets", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    for (int offset = 0; offset < 4; ++offset) {
        CAPTURE(rate, offset);
        const auto x = burstInSine(rate, offset);
        // the synthesis is right: an independent 16x interpolation reads the burst at full scale
        REQUIRE_THAT(truePeakDB(x), WithinAbs(0.0, .2));
        const auto peak = meterTruePeak(x, rate);
        CHECK(peak <= .2f);
        CHECK(peak >= -.4f);
    }
}

TEST_CASE("EBU Tech 3342 cases 1 to 4: loudness range", "[loudness]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    CAPTURE(rate);
    const std::array<std::pair<Programme, double>, 4> cases{
        {
            {{{20.0, -20.f}, {20.0, -30.f}}, 10.0},
            {{{20.0, -20.f}, {20.0, -15.f}}, 5.0},
            {{{20.0, -40.f}, {20.0, -20.f}}, 20.0},
            {{{20.0, -50.f}, {20.0, -35.f}, {20.0, -20.f}, {20.0, -35.f}, {20.0, -50.f}}, 15.0},
        }
    };
    for (size_t k = 0; k < cases.size(); ++k) {
        CAPTURE(k + 1);
        const auto& [programme, expected] = cases[k];
        CHECK_THAT(measure(programme, rate).range, WithinAbs(expected, 1.0));
        // "the expected response is unchanged if the test signal is repeated"
        if (static_cast<int>(rate) == 48000) {
            CHECK_THAT(measure(repeat(programme, 2), rate).range, WithinAbs(expected, 1.0));
        }
    }
}

TEST_CASE("the meter does not depend on the block size", "[loudness]") {
    const auto a = measure({{5.0, -18.f}}, kSampleRate, 64);
    const auto b = measure({{5.0, -18.f}}, kSampleRate, 4096);
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

TEST_CASE("a host reset between runs starts the measurement over", "[loudness]") {
    for (const auto auto_reset : {true, false}) {
        PluginProcessor p;
        FakePlayHead playhead;
        p.setPlayHead(&playhead);
        set(p.na_parameters_, zlstate::PLoudnessAutoReset::kID, auto_reset ? 1.f : 0.f);
        prepare(p);
        playhead.playing = true;
        run(p, sine(1000.0, -23.f), 2.0);
        const auto generation = p.pullLoudness().generation;
        // some hosts reset the plug-in between runs without sending stopped blocks
        p.reset();
        run(p, sine(1000.0, -23.f), .5);
        INFO("auto reset " << auto_reset);
        CHECK((p.pullLoudness().generation > generation) == auto_reset);
        p.setPlayHead(nullptr);
    }
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
