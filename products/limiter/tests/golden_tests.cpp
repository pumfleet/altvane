// Copyright (C) 2026 - Altvane, based on ZLLimiter by zsliu98
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

/**
 * golden behaviour of the vendored engine: CI only checks that source/dsp, source/zlp and source/chore match the
 * pinned upstream commit, so these pin down what it sounds like. A sync that moves any of them changed the sound:
 * listen, and update the numbers here (and the CHANGELOG) only if the change is wanted.
 *
 * everything is synthesised with fixed seeds, so the numbers do not depend on the machine beyond floating-point
 * rounding (the tolerances leave room for that, and for SIMD differences between platforms)
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "test_helpers.hpp"
#include "ui/model/character.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;

namespace {
    using StereoGenerator = std::function<std::pair<float, float>(int64_t)>;

    void prepareAt(PluginProcessor& p, const double rate, const int block = kBlock) {
        p.setRateAndBufferSizeDetails(rate, block);
        p.prepareToPlay(rate, block);
    }

    void setCharacter(PluginProcessor& p, const char* name) {
        for (const auto& c : altvane::limiter::kCharacters) {
            if (juce::String(c.name) == name) {
                const auto v = altvane::limiter::characterValues(c);
                for (size_t k = 0; k < v.size(); ++k) {
                    set(p, altvane::limiter::kCharacterParameterIDs[k], v[k]);
                }
                return;
            }
        }
        FAIL("no character " << name);
    }

    /**
     * a drum loop with a bass line and bright noise on top, at a sample rate (deterministic: juce::Random is an LCG)
     */
    StereoGenerator drumLoop(const double rate, const int seed = 3) {
        auto random = std::make_shared<juce::Random>(seed);
        return [=](const int64_t n) {
            constexpr double kBeat = .5;
            const auto t = static_cast<double>(n) / rate;
            const auto in_beat = std::fmod(t, kBeat);
            const auto kick = std::sin(2.0 * std::numbers::pi * (50.0 + 120.0 * std::exp(-in_beat * 40.0)) * in_beat) *
                              std::exp(-in_beat * 9.0);
            const auto snare_t = std::fmod(t + kBeat, 2.0 * kBeat);
            const auto noise = random->nextDouble() * 2.0 - 1.0;
            const auto snare = noise * std::exp(-snare_t * 22.0) * .7;
            const auto hat = (random->nextDouble() * 2.0 - 1.0) * std::exp(-std::fmod(t, kBeat * .5) * 60.0) * .25;
            const auto bass = .35 * std::sin(2.0 * std::numbers::pi * 55.0 * t);
            const auto l = .5 * (.55 * kick + .32 * snare + bass + hat);
            const auto r = .5 * (.55 * kick + .28 * snare + bass - hat);
            return std::pair{static_cast<float>(l), static_cast<float>(r)};
        };
    }

    std::array<std::vector<float>, 2> render(PluginProcessor& p, const double rate, const StereoGenerator& gen,
                                             const double seconds, const int block = kBlock) {
        const auto total = static_cast<int64_t>(seconds * rate);
        std::array<std::vector<float>, 2> out;
        juce::AudioBuffer<float> buffer(2, block);
        juce::MidiBuffer midi;
        for (int64_t start = 0; start < total; start += block) {
            const auto n = static_cast<int>(std::min<int64_t>(block, total - start));
            buffer.setSize(2, n, false, false, true);
            for (int i = 0; i < n; ++i) {
                const auto [l, r] = gen(start + i);
                buffer.setSample(0, i, l);
                buffer.setSample(1, i, r);
            }
            p.processBlock(buffer, midi);
            for (int c = 0; c < 2; ++c) {
                out[static_cast<size_t>(c)].insert(out[static_cast<size_t>(c)].end(), buffer.getReadPointer(c),
                                                   buffer.getReadPointer(c) + n);
            }
        }
        return out;
    }

    /**
     * gain reduction (dB, positive) over windows: the gained input against the output, lined up by the latency
     */
    std::vector<float> reductionCurve(const std::vector<float>& input, const float gain_db, const std::vector<float>& output,
                                      const size_t latency, const size_t window, const size_t from) {
        const auto gain = juce::Decibels::decibelsToGain(static_cast<double>(gain_db));
        std::vector<float> curve;
        for (auto start = from; start + window + latency <= output.size(); start += window) {
            double in = 0.0, out = 0.0;
            for (size_t i = start; i < start + window; ++i) {
                in += gain * input[i] * gain * input[i];
                out += static_cast<double>(output[i + latency]) * output[i + latency];
            }
            curve.push_back(static_cast<float>(10.0 * std::log10(std::max(in, 1e-30) / std::max(out, 1e-30))));
        }
        return curve;
    }

    std::vector<float> channelOf(const StereoGenerator& gen, const size_t channel, const size_t length) {
        std::vector<float> x(length);
        for (size_t i = 0; i < length; ++i) {
            const auto v = gen(static_cast<int64_t>(i));
            x[i] = channel == 0 ? v.first : v.second;
        }
        return x;
    }
}

TEST_CASE("golden: the ceiling holds at every oversampling factor, at 44.1 and 96 kHz", "[golden]") {
    const auto rate = GENERATE(44100.0, 96000.0);
    const auto oversampling = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(rate, oversampling);
    // the drum loop with a bright sine near the top of the band, pushed 12 dB into a -1 dB ceiling
    const auto loop = drumLoop(rate);
    const auto bright = [&](const int64_t n) {
        const auto [l, r] = loop(n);
        const auto s = static_cast<float>(.25 * std::sin(2.0 * std::numbers::pi * .23 * static_cast<double>(n) + .7));
        return std::pair{l + s, r - s};
    };
    for (const auto true_peak : {false, true}) {
        CAPTURE(true_peak);
        PluginProcessor p;
        set(p, zlp::POversampling::kID, static_cast<float>(oversampling));
        set(p, zlp::PTruePeak::kID, true_peak ? 1.f : 0.f);
        set(p, zlp::POutputCeiling::kID, -1.f);
        set(p, zlp::PInputGain::kID, 12.f);
        prepareAt(p, rate);
        const auto out = render(p, rate, bright, 1.5);
        const auto skip = static_cast<size_t>(p.getLatencySamples()) + static_cast<size_t>(rate * .1);
        for (size_t c = 0; c < 2; ++c) {
            CAPTURE(c);
            // the sample peak never passes the ceiling, and the limiter limits rather than mutes
            const auto sample_peak = peakDB(out[c], skip);
            CHECK(sample_peak <= -1.f + 1e-4f);
            CHECK(sample_peak > -2.5f);
            // in true peak mode the reconstructed waveform obeys it too, within what the engine's own estimator
            // misses (the independent 16x check reads up to 0.17 dB over, without oversampling at 96 kHz)
            if (true_peak) {
                CHECK(truePeakDB(out[c], skip) <= -1.f + .25f);
            }
        }
    }
}

TEST_CASE("golden: latency at every oversampling factor", "[golden]") {
    struct Golden {
        double rate;
        std::array<int, 6> latency;
    };
    for (const auto& [rate, latency] : {Golden{44100.0, {423, 487, 495, 499, 501, 502}},
                                        Golden{48000.0, {442, 506, 514, 518, 520, 521}},
                                        Golden{96000.0, {694, 758, 766, 770, 772, 773}}}) {
        for (size_t oversampling = 0; oversampling < latency.size(); ++oversampling) {
            CAPTURE(rate, oversampling);
            PluginProcessor p;
            set(p, zlp::POversampling::kID, static_cast<float>(oversampling));
            prepareAt(p, rate);
            CHECK(p.getLatencySamples() == latency[oversampling]);
        }
    }
}

TEST_CASE("golden: gain reduction of a known programme", "[golden]") {
    // Balanced (the engine's defaults), the drum loop pushed 10 dB into -1 dB, true peak, 4x: the gain reduction over
    // 250 ms windows from 0.5 s, against what this engine version measured
    constexpr double kRate = 48000.0;
    PluginProcessor p;
    setCharacter(p, "Balanced");
    set(p, zlp::POversampling::kID, 2.f);
    set(p, zlp::PTruePeak::kID, 1.f);
    set(p, zlp::POutputCeiling::kID, -1.f);
    set(p, zlp::PInputGain::kID, 10.f);
    prepareAt(p, kRate);
    const auto gen = drumLoop(kRate);
    const auto out = render(p, kRate, gen, 4.5);
    const auto latency = static_cast<size_t>(p.getLatencySamples());
    const auto input = channelOf(drumLoop(kRate), 0, out[0].size());
    const auto curve = reductionCurve(input, 10.f, out[0], latency, 12000, 24000);
    // measured on this engine version (ZL Limiter 412c844)
    const std::vector<float> golden{
        2.97f, 1.04f, 2.53f, .91f, 3.01f, 1.00f, 2.54f, .89f, 2.78f, 1.00f, 2.83f, .87f, 2.87f, .98f, 2.68f
    };
    REQUIRE(curve.size() >= golden.size());
    for (size_t k = 0; k < golden.size(); ++k) {
        CAPTURE(k, curve[k]);
        CHECK_THAT(curve[k], WithinAbs(golden[k], .15));
    }
}

TEST_CASE("golden: recovery after a loud passage, per character", "[golden]") {
    // a 1 kHz tone 7 dB over the ceiling for a second, then 30 dB lower: how long the gain takes to come back within
    // 1 dB, against what this engine version measured
    constexpr double kRate = 48000.0;
    const auto tone = [](const int64_t n) {
        const auto amp = n < static_cast<int64_t>(kRate) ? .5 : .5 * std::pow(10.0, -30.0 / 20.0);
        const auto v = static_cast<float>(amp * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(n) / kRate));
        return std::pair{v, v};
    };
    // measured on this engine version (ZL Limiter 412c844), in 5 ms steps
    const std::array<std::pair<const char*, double>, 5> golden{
        {{"Aggressive", .04}, {"Punchy", .28}, {"Balanced", .475}, {"Transparent", .93}, {"Safe", 1.41}}
    };
    for (const auto& [name, golden_seconds] : golden) {
        PluginProcessor p;
        setCharacter(p, name);
        set(p, zlp::POutputCeiling::kID, -1.f);
        set(p, zlp::PInputGain::kID, 12.f);
        prepareAt(p, kRate);
        const auto out = render(p, kRate, tone, 3.0);
        const auto latency = static_cast<size_t>(p.getLatencySamples());
        const auto input = channelOf(tone, 0, out[0].size());
        // 5 ms windows (whole cycles), from the drop
        const auto curve = reductionCurve(input, 12.f, out[0], latency, 240, 48000);
        size_t k = 0;
        while (k < curve.size() && curve[k] > 1.f) {
            ++k;
        }
        const auto seconds = static_cast<double>(k) * .005;
        CAPTURE(name, seconds);
        // the tone was held 7 dB down before the drop
        CHECK_THAT(reductionCurve(input, 12.f, out[0], latency, 240, 40000)[0], WithinAbs(7.1, .15));
        CHECK_THAT(seconds, WithinAbs(golden_seconds, std::max(.1 * golden_seconds, .011)));
    }
}

TEST_CASE("golden: channel delta sets how far the channels' reduction may differ", "[golden]") {
    // the drum loop on the left and 12 dB lower on the right: linked, the right is turned down with the left;
    // unlinked, it keeps more of its level
    constexpr double kRate = 48000.0;
    // (each use needs a fresh generator: the noise in the loop comes from one random sequence)
    const auto programme = [] {
        const auto loop = drumLoop(kRate);
        return StereoGenerator([loop](const int64_t n) {
            const auto l = loop(n).first;
            return std::pair{l, .25f * l};
        });
    };
    // measured on this engine version (ZL Limiter 412c844): the mean reduction over 3.5 s of each channel
    struct Golden {
        float delta, right;
    };
    for (const auto [delta, golden_right] : {Golden{0.f, 1.77f}, Golden{1.5f, 1.62f}, Golden{3.f, 1.53f},
                                             Golden{6.f, 1.44f}}) {
        PluginProcessor p;
        set(p, zlp::PChannelDelta::kID, delta);
        set(p, zlp::POutputCeiling::kID, -1.f);
        set(p, zlp::PInputGain::kID, 10.f);
        prepareAt(p, kRate);
        const auto out = render(p, kRate, programme(), 4.5);
        const auto latency = static_cast<size_t>(p.getLatencySamples());
        const auto left_input = channelOf(programme(), 0, out[0].size());
        const auto right_input = channelOf(programme(), 1, out[0].size());
        const auto left = reductionCurve(left_input, 10.f, out[0], latency, 4 * 48000 - 2 * 12000, 24000)[0];
        const auto right = reductionCurve(right_input, 10.f, out[1], latency, 4 * 48000 - 2 * 12000, 24000)[0];
        CAPTURE(delta, left, right);
        CHECK_THAT(left, WithinAbs(1.77, .05));
        CHECK_THAT(right, WithinAbs(golden_right, .05));
        if (delta < .01f) {
            CHECK_THAT(right, WithinAbs(left, .02));
        }
    }
}

TEST_CASE("facts for docs/BENCHMARK.md: latency and the ceiling's true-peak margin", "[.facts]") {
    // not run by default: `AltvaneLimiterTests "[.facts]"` prints what docs/BENCHMARK.md lists
    for (const auto rate : {44100.0, 48000.0, 96000.0}) {
        for (int oversampling = 0; oversampling < 6; ++oversampling) {
            std::array<float, 2> over{};
            int latency = 0;
            for (const auto true_peak : {false, true}) {
                const auto loop = drumLoop(rate);
                const auto bright = [&](const int64_t n) {
                    const auto [l, r] = loop(n);
                    const auto s = static_cast<float>(.25 * std::sin(2.0 * std::numbers::pi * .23 * static_cast<double>(n) + .7));
                    return std::pair{l + s, r - s};
                };
                PluginProcessor p;
                set(p, zlp::POversampling::kID, static_cast<float>(oversampling));
                set(p, zlp::PTruePeak::kID, true_peak ? 1.f : 0.f);
                set(p, zlp::POutputCeiling::kID, -1.f);
                set(p, zlp::PInputGain::kID, 12.f);
                prepareAt(p, rate);
                latency = p.getLatencySamples();
                const auto out = render(p, rate, bright, 3.0);
                const auto skip = static_cast<size_t>(latency) + static_cast<size_t>(rate * .1);
                over[true_peak ? 1 : 0] = std::max(truePeakDB(out[0], skip), truePeakDB(out[1], skip)) + 1.f;
            }
            WARN(rate << " Hz, oversampling " << zlp::POversampling::kChoices[oversampling] << ": latency " << latency
                 << " samples (" << 1000.0 * latency / rate << " ms); true peak over a -1 dB ceiling " << over[0]
                 << " dB in sample peak mode, " << over[1] << " dB in true peak mode");
        }
    }
}
