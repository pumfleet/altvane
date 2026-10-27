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

#include "alloc_counter.hpp"
#include "test_helpers.hpp"
#include "ui/model/character.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;
namespace params = altvane::limiter::params;

TEST_CASE("state round trips, with ZL Limiter's tags and Altvane's additions", "[state]") {
    PluginProcessor a;
    set(a, zlp::PInputGain::kID, 7.5f);
    set(a, zlp::POutputCeiling::kID, -1.f);
    set(a, zlp::POversampling::kID, 2.f);
    set(a, params::PDitherBits::kID, 3.f);
    set(a, params::PDCFilter::kID, 1.f);
    set(a.na_parameters_, zlstate::PLoudnessTarget::kID, 3.f);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName(zlstate::schema::kProcessorState));
    CHECK(xml->getChildByName(zlstate::schema::kParameterState) != nullptr);
    CHECK(xml->getChildByName(zlstate::schema::kNonAutomatableState) != nullptr);

    PluginProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto raw = [&](const char* id) { return b.parameters_.getRawParameterValue(id)->load(); };
    CHECK_THAT(raw(zlp::PInputGain::kID), WithinAbs(7.5, 1e-3));
    CHECK_THAT(raw(zlp::POutputCeiling::kID), WithinAbs(-1.0, 1e-3));
    CHECK(raw(zlp::POversampling::kID) == 2.f);
    CHECK(raw(params::PDitherBits::kID) == 3.f);
    CHECK(raw(params::PDCFilter::kID) == 1.f);
    CHECK(b.na_parameters_.getRawParameterValue(zlstate::PLoudnessTarget::kID)->load() == 3.f);
}

TEST_CASE("presets are ZL Limiter preset files", "[state]") {
    PluginProcessor a;
    set(a, zlp::PInputGain::kID, 4.f);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    const auto file = juce::File::createTempFile(".json");
    REQUIRE(zlstate::PresetJson::write(file, state).wasOk());
    CHECK(file.loadFileAsString().contains("ZLLimiterPreset"));

    juce::MemoryBlock loaded;
    REQUIRE(zlstate::PresetJson::read(file, loaded).wasOk());
    PluginProcessor b;
    b.setStateInformation(loaded.getData(), static_cast<int>(loaded.getSize()));
    CHECK_THAT(b.parameters_.getRawParameterValue(zlp::PInputGain::kID)->load(), WithinAbs(4.0, 1e-3));
    file.deleteFile();
}

TEST_CASE("latency is reported and grows with oversampling", "[latency]") {
    PluginProcessor p;
    prepare(p);
    const auto base = p.getLatencySamples();
    CHECK(base > 0);
    // the lookahead is a fixed delay inside the engine, so moving it never changes the latency
    set(p, zlp::PLookahead::kID, 0.5f);
    run(p, sine(1000.0, -20.f), .05);
    pumpMessages();
    CHECK(p.getLatencySamples() == base);

    set(p, zlp::POversampling::kID, 2.f);
    run(p, sine(1000.0, -20.f), .05);
    pumpMessages();
    CHECK(p.getLatencySamples() > base);
}

TEST_CASE("the output never passes the ceiling", "[limiter]") {
    PluginProcessor p;
    set(p, zlp::PTruePeak::kID, 0.f);
    set(p, zlp::POutputCeiling::kID, -1.f);
    set(p, zlp::PInputGain::kID, 12.f);
    prepare(p);
    const auto out = run(p, [](const int64_t n) {
        // a loud sine with bursts of noise on top, both channels different
        const auto t = static_cast<double>(n) / kSampleRate;
        const auto s = .5 * std::sin(2.0 * std::numbers::pi * 220.0 * t);
        const auto k = static_cast<double>(n);
        const auto burst = (n / 4800) % 3 == 0 ? .4 * std::sin(k * 1.7) * std::cos(k * .31) : 0.0;
        return std::pair{static_cast<float>(s + burst), static_cast<float>(-s + .5 * burst)};
    }, 2.0);
    const auto skip = static_cast<size_t>(p.getLatencySamples()) + 4800;
    CHECK(peakDB(out[0], skip) <= -1.f + 1e-3f);
    CHECK(peakDB(out[1], skip) <= -1.f + 1e-3f);
    // and it limits rather than mutes: a 12 dB push of a -6 dBFS sine sits right under the ceiling
    CHECK(peakDB(out[0], skip) > -2.f);
}

TEST_CASE("true peak mode catches inter-sample peaks", "[limiter]") {
    // a sine at a quarter of the sample rate, sampled 45 degrees off its peaks: samples at 0.707 of the true peak
    const auto signal = sine(kSampleRate / 4.0, 0.f, std::numbers::pi / 4.0);
    auto measure = [&](const bool true_peak) {
        PluginProcessor p;
        set(p, zlp::PTruePeak::kID, true_peak ? 1.f : 0.f);
        set(p, zlp::POutputCeiling::kID, -1.f);
        set(p, zlp::PInputGain::kID, 0.f);
        prepare(p);
        const auto out = run(p, signal, .5);
        const auto skip = static_cast<size_t>(p.getLatencySamples()) + 9600;
        return std::pair{peakDB(out[0], skip), truePeakDB(out[0], skip)};
    };
    const auto [sample_off, true_off] = measure(false);
    const auto [sample_on, true_on] = measure(true);
    // without true peak the samples obey the ceiling but the reconstructed waveform goes about 3 dB over
    CHECK(sample_off <= -1.f + 1e-3f);
    CHECK(true_off > -0.5f);
    // with true peak the waveform obeys it too
    CHECK(sample_on <= -1.f + 1e-3f);
    CHECK(true_on <= -1.f + .1f);
}

TEST_CASE("unity gain takes the input gain back off the output", "[limiter]") {
    PluginProcessor p;
    set(p, zlp::PInputGain::kID, 6.f);
    set(p, zlp::PInputOutputLink::kID, 1.f);
    prepare(p);
    // quiet enough that nothing is limited
    const auto out = run(p, sine(1000.0, -30.f), 1.0);
    CHECK_THAT(peakDB(out[0], 24000), WithinAbs(-30.0, .1));
}

TEST_CASE("delta plays only what the limiter removes", "[limiter]") {
    PluginProcessor p;
    set(p, zlp::PDelta::kID, 1.f);
    prepare(p);
    // nothing to remove from a quiet signal
    const auto quiet = run(p, sine(1000.0, -30.f), 1.0);
    CHECK(peakDB(quiet[0], 24000) < -100.f);
    // something to remove from a loud one
    set(p, zlp::PInputGain::kID, 12.f);
    const auto loud = run(p, sine(1000.0, -6.f), 1.0);
    CHECK(peakDB(loud[0], 24000) > -20.f);
}

TEST_CASE("turning audition on never lets the limited signal through", "[limiter]") {
    // with the editor closed the engine's delayed copy of the gained input is idle; switching Audition on while
    // a signal plays must not leak the processed output while that copy fills
    PluginProcessor p;
    prepare(p);
    const auto signal = sine(1000.0, -30.f);
    run(p, signal, .5);
    set(p, zlp::PDelta::kID, 1.f);
    const auto out = run(p, [&](const int64_t n) { return signal(n + 24000); }, .3);
    CHECK(peakDB(out[0]) < -80.f);
    CHECK(peakDB(out[1]) < -80.f);
}

TEST_CASE("bypass passes the input, delayed by the latency", "[limiter]") {
    for (const auto host : {false, true}) {
        PluginProcessor p;
        set(p, zlp::PInputGain::kID, 12.f);
        set(p, params::PDitherBits::kID, 3.f);
        set(p, params::PDCFilter::kID, 1.f);
        if (!host) {
            set(p, zlp::PBypass::kID, 1.f);
        }
        prepare(p);
        const auto signal = sine(997.0, -3.f);
        const auto out = run(p, signal, .5, host);
        const auto latency = static_cast<int64_t>(p.getLatencySamples());
        float worst = 0.f;
        for (int64_t n = latency + 1000; n < static_cast<int64_t>(out[0].size()); ++n) {
            worst = std::max(worst, std::abs(out[0][static_cast<size_t>(n)] - signal(n - latency).first));
        }
        INFO("host bypass " << host);
        CHECK(worst < 1e-6f);
    }
}

TEST_CASE("dither is bit-exact when off and lands on the bit depth's grid when on", "[output]") {
    const auto noise_floor = [](const int bits_index, const int shape, std::vector<float>* out_samples = nullptr) {
        PluginProcessor p;
        set(p, params::PDitherBits::kID, static_cast<float>(bits_index));
        set(p, params::PDitherShape::kID, static_cast<float>(shape));
        prepare(p);
        const auto out = run(p, [](int64_t) { return std::pair{0.f, 0.f}; }, 1.0);
        if (out_samples != nullptr) {
            *out_samples = out[0];
        }
        return rmsDB(out[0], 4800);
    };
    CHECK(noise_floor(0, 0) < -200.f);

    // 16 bit, flat TPDF: quantisation (LSB^2 / 12) plus dither (LSB^2 / 6) is LSB / 2 RMS, about -96.3 dBFS
    std::vector<float> flat;
    CHECK_THAT(noise_floor(3, 0, &flat), WithinAbs(-96.3, .5));
    for (size_t n = 4800; n < flat.size(); ++n) {
        const auto steps = static_cast<double>(flat[n]) * 32768.0;
        REQUIRE(steps == std::round(steps));
    }
    // 24 bit is 48 dB lower
    CHECK_THAT(noise_floor(1, 0), WithinAbs(-144.5, .5));

    // shaping moves noise out of the low and middle frequencies: compare the noise under 4 kHz
    // (a 255-tap Blackman-windowed sinc low-pass, so the shaped noise near Nyquist cannot leak in)
    const auto low_band = [](const std::vector<float>& x) {
        constexpr int kHalf = 127;
        constexpr double kCutoff = 4000.0 / kSampleRate;
        std::vector<double> h;
        for (int k = -kHalf; k <= kHalf; ++k) {
            const auto sinc = k == 0 ? 2.0 * kCutoff
                                     : std::sin(2.0 * std::numbers::pi * kCutoff * k) / (std::numbers::pi * k);
            const auto t = static_cast<double>(k + kHalf) / (2.0 * kHalf);
            h.push_back(sinc * (.42 - .5 * std::cos(2.0 * std::numbers::pi * t) + .08 * std::cos(4.0 * std::numbers::pi * t)));
        }
        std::vector<float> filtered;
        for (size_t n = 2 * kHalf; n < x.size(); ++n) {
            double sum = 0.0;
            for (size_t k = 0; k < h.size(); ++k) {
                sum += h[k] * x[n - k];
            }
            filtered.push_back(static_cast<float>(sum));
        }
        return rmsDB(filtered, 4800);
    };
    std::vector<float> shaped;
    noise_floor(3, 1, &shaped);
    CHECK(low_band(shaped) < low_band(flat) - 6.f);
}

TEST_CASE("dither stays off while bypassed and in delta", "[output]") {
    for (const auto* id : {zlp::PBypass::kID, zlp::PDelta::kID}) {
        PluginProcessor p;
        set(p, params::PDitherBits::kID, 3.f);
        set(p, id, 1.f);
        prepare(p);
        const auto out = run(p, [](int64_t) { return std::pair{0.f, 0.f}; }, .3);
        INFO(id);
        CHECK(peakDB(out[0]) <= -200.f);
    }
}

TEST_CASE("the DC filter removes an offset", "[output]") {
    PluginProcessor p;
    set(p, params::PDCFilter::kID, 1.f);
    prepare(p);
    const auto tone = sine(200.0, -12.f);
    const auto out = run(p, [&](const int64_t n) {
        const auto v = tone(n).first + .2f;
        return std::pair{v, v};
    }, 3.0);
    // one second of whole 200 Hz cycles at the end
    double mean = 0.0;
    for (size_t n = out[0].size() - 48000; n < out[0].size(); ++n) {
        mean += out[0][n];
    }
    CHECK(std::abs(mean / 48000.0) < 1e-3);
}

TEST_CASE("host blocks larger than the prepared size are processed in pieces", "[realtime]") {
    // JUCE allows a host to send more than prepareToPlay announced; the engine's buffers only hold that much
    const auto render = [](const int host_block) {
        PluginProcessor p;
        set(p, zlp::PInputGain::kID, 9.f);
        prepare(p, 256);
        return run(p, sine(440.0, -6.f), 1.0, false, host_block);
    };
    const auto small = render(256);
    const auto large = render(1500);
    REQUIRE(small[0].size() == large[0].size());
    float worst = 0.f;
    for (size_t n = 0; n < small[0].size(); ++n) {
        worst = std::max({worst, std::abs(small[0][n] - large[0][n]), std::abs(small[1][n] - large[1][n])});
    }
    CHECK(worst < 1e-6f);
}

TEST_CASE("the audio thread does not allocate", "[realtime]") {
    PluginProcessor p;
    set(p, zlp::PInputGain::kID, 12.f);
    set(p, params::PDitherBits::kID, 3.f);
    set(p, params::PDCFilter::kID, 1.f);
    prepare(p);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    const auto signal = sine(440.0, -3.f);
    size_t allocations = 0;
    for (int b = 0; b < 400; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const auto v = signal(b * kBlock + i).first;
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        const alloc_counter::Scope scope;
        p.processBlock(buffer, midi);
        allocations += scope.count();
    }
    CHECK(allocations == 0);
}

TEST_CASE("characters run from gentle to loud", "[limiter]") {
    // at the same gain on a drum loop: Safe, Transparent, Balanced, Punchy, Aggressive
    const auto measure = [](const altvane::limiter::Character& c) {
        PluginProcessor p;
        const auto v = altvane::limiter::characterValues(c);
        for (size_t k = 0; k < v.size(); ++k) {
            set(p, altvane::limiter::kCharacterParameterIDs[k], v[k]);
        }
        set(p, zlp::PInputGain::kID, 10.f);
        set(p, zlp::POutputCeiling::kID, -1.f);
        prepare(p);
        juce::Random random(3);
        constexpr double kBeat = .5;
        const auto out = run(p, [&](const int64_t n) {
            const auto t = static_cast<double>(n) / kSampleRate;
            const auto in_beat = std::fmod(t, kBeat);
            const auto kick = std::sin(2.0 * std::numbers::pi * (50.0 + 120.0 * std::exp(-in_beat * 40.0)) * in_beat) *
                              std::exp(-in_beat * 9.0);
            const auto snare_t = std::fmod(t + kBeat, 2.0 * kBeat);
            const auto snare = (random.nextDouble() * 2.0 - 1.0) * std::exp(-snare_t * 22.0) * .7;
            const auto bass = .35 * std::sin(2.0 * std::numbers::pi * 55.0 * t);
            const auto l = static_cast<float>(.5 * (.55 * kick + .32 * snare + bass));
            return std::pair{l, .9f * l};
        }, 8.0);
        altvane::limiter::LoudnessMeter meter;
        constexpr size_t kSkip = 2 * 48000;
        meter.prepare(kSampleRate, 2, out[0].size());
        const std::array<const float*, 2> channels{out[0].data() + kSkip, out[1].data() + kSkip};
        meter.process(channels, out[0].size() - kSkip);
        return meter.pull().integrated;
    };
    using altvane::limiter::kCharacters;
    const std::array order{4, 0, 1, 2, 3};
    auto previous = -100.f;
    for (const auto i : order) {
        const auto loudness = measure(kCharacters[static_cast<size_t>(i)]);
        INFO(kCharacters[static_cast<size_t>(i)].name << " " << loudness << " LUFS");
        CHECK(loudness > previous + .1f);
        previous = loudness;
    }
}
