// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "alloc_counter.hpp"
#include "test_helpers.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;

namespace {
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
     * a busy setting: lookahead, oversampling, RMS, hold, a side-chain EQ band, a clipper
     */
    void makeBusy(PluginProcessor& p) {
        set(p, zlp::PThreshold::kID, -30.f);
        set(p, zlp::PRatio::kID, 6.f);
        set(p, zlp::PAttack::kID, 3.f);
        set(p, zlp::PRelease::kID, 80.f);
        set(p, zlp::PHold::kID, 5.f);
        set(p, zlp::PLookAhead::kID, 2.f);
        set(p, zlp::POversample::kID, 1.f);
        set(p, zlp::PRMSON::kID, 1.f);
        set(p, zlp::PClipperDrive::kID, 30.f);
        set(p, zlp::POutGain::kID, 6.f);
        set(p, zlp::PFilterType::kID + juce::String(0), static_cast<float>(zldsp::filter::FilterType::kHighPass));
        set(p, zlp::PFreq::kID + juce::String(0), 150.f);
        set(p, zlp::PFilterStatus::kID + juce::String(0), 2.f);
    }

    /**
     * a 1 kHz sine at the given peak level for the given time; the output peak (dBFS) over the last half second
     */
    float runSine(PluginProcessor& p, const float peak_db, const double length) {
        const auto out = run(p, {sine(1000.0, peak_db)}, seconds(length));
        return peakDB(out[0], out[0].size() - static_cast<size_t>(seconds(.5)));
    }

    template <typename T>
    float worstDifference(const std::vector<std::vector<T>>& a, const std::vector<std::vector<T>>& b) {
        REQUIRE(a.size() == b.size());
        double worst = 0.0;
        for (size_t c = 0; c < a.size(); ++c) {
            REQUIRE(a[c].size() == b[c].size());
            for (size_t n = 0; n < a[c].size(); ++n) {
                worst = std::max(worst, std::abs(static_cast<double>(a[c][n]) - static_cast<double>(b[c][n])));
            }
        }
        return static_cast<float>(worst);
    }

    // every parameter ZL Compressor 0.6.1 has, in its order; sessions and presets refer to these
    const juce::StringArray& frozenIDs() {
        static const auto ids = [] {
            juce::StringArray result{
                "comp_style", "comp_direction", "threshold", "ratio", "knee_width", "curve", "floor", "attack",
                "release", "pump", "smooth", "hold", "range", "wet", "makeup", "external_side", "side_out",
                "side gain", "side_eq_bypass", "side_stereo_mode", "side_stereo_swap", "side_stereo_link",
                "side_stereo_wet1", "side_stereo_wet2", "comp_on", "comp_delta", "clipper_drive", "oversample",
                "lookahead", "rms_on", "rms_length", "rms_speed", "rms_mix", "range_inf"
            };
            for (int band = 0; band < 8; ++band) {
                for (const auto* id : {"filter_status", "filter_type", "order", "freq", "gain", "Q"}) {
                    result.add(id + juce::String(band));
                }
            }
            return result;
        }();
        return ids;
    }
}

TEST_CASE("parameter IDs never change", "[state]") {
    PluginProcessor p;
    juce::StringArray ids;
    for (auto* para : p.getParameters()) {
        ids.add(dynamic_cast<juce::RangedAudioParameter*>(para)->getParameterID());
    }
    // this list may only grow
    CHECK(ids == frozenIDs());
}

TEST_CASE("state round trip keeps every parameter", "[state]") {
    PluginProcessor a;
    // every parameter away from its default, on both trees
    for (auto* tree : {&a.parameters_, &a.na_parameters_}) {
        auto& processor = tree == &a.parameters_ ? static_cast<juce::AudioProcessor&>(a) : a.dummy_processor_;
        for (auto* para : processor.getParameters()) {
            // through the plain value, so a switch or a choice lands on one of its steps
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(para);
            const auto plain = ranged->convertFrom0to1(para->getDefaultValue() < .5f ? .73f : .21f);
            para->setValueNotifyingHost(ranged->convertTo0to1(plain));
        }
    }
    juce::MemoryBlock block;
    a.getStateInformation(block);

    PluginProcessor b;
    b.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    for (const auto& [from, to] : {
             std::pair{&a.parameters_, &b.parameters_}, std::pair{&a.na_parameters_, &b.na_parameters_}
         }) {
        auto& processor = from == &a.parameters_ ? static_cast<juce::AudioProcessor&>(a) : a.dummy_processor_;
        for (auto* para : processor.getParameters()) {
            const auto id = dynamic_cast<juce::RangedAudioParameter*>(para)->getParameterID();
            CAPTURE(id);
            const auto plain = from->getRawParameterValue(id)->load();
            REQUIRE(std::abs(para->getValue() - para->getDefaultValue()) > 1e-3f);
            CHECK_THAT(to->getRawParameterValue(id)->load(), WithinAbs(plain, 1e-4 * std::max(1.f, std::abs(plain))));
        }
    }
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
    CHECK_THAT(runSine(p, -10.f, 2.0), WithinAbs(-25.0, .5));

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

TEST_CASE("host bypass passes the input, delayed by the reported latency", "[bypass]") {
    // what could otherwise be heard: the engine's own delay and filters, delta, the side-chain listen, a solo
    enum Setting { kPlain, kLatency, kDelta, kListen, kSolo };
    const auto [main_channels, aux_channels] = GENERATE(std::pair{2, 2}, std::pair{2, 1}, std::pair{2, 0},
                                      std::pair{1, 2}, std::pair{1, 1}, std::pair{1, 0});
    const auto setting = GENERATE(kPlain, kLatency, kDelta, kListen, kSolo);
    const auto double_precision = GENERATE(false, true);
    CAPTURE(main_channels, aux_channels, setting, double_precision);

    PluginProcessor p;
    setLayout(p, main_channels, aux_channels);
    makeStatic(p, -40.f, 10.f);
    set(p, zlp::POutGain::kID, 6.f);
    if (setting == kLatency) {
        set(p, zlp::PLookAhead::kID, 5.f);
        set(p, zlp::POversample::kID, 2.f);
    } else if (setting == kDelta) {
        set(p, zlp::PCompDelta::kID, 1.f);
    } else if (setting == kListen) {
        set(p, zlp::PExtSide::kID, 1.f);
        set(p, zlp::PSideOut::kID, 1.f);
    } else if (setting == kSolo) {
        set(p, zlp::PFreq::kID + juce::String(0), 300.f);
        set(p, zlp::PFilterStatus::kID + juce::String(0), 2.f);
        p.getEqualizeController().setSoloBand(0);
    }
    prepare(p);

    Run r{sine(997.0, -3.f), sine(300.0, -6.f)};
    const auto check = [&]<typename T>(T) {
        // the engine reports its latency from the message thread
        run<T>(p, r, seconds(.1));
        pumpMessages();
        const auto latency = static_cast<int64_t>(p.getLatencySamples());
        if (setting == kLatency) {
            CHECK(latency > 240);
        }
        r.start = seconds(.1);
        r.bypassed = true;
        const auto out = run<T>(p, r, seconds(.3));
        double worst = 0.0;
        for (const auto& channel : out) {
            for (size_t n = 0; n < channel.size(); ++n) {
                const auto expected = static_cast<double>(r.main(r.start + static_cast<int64_t>(n) - latency));
                worst = std::max(worst, std::abs(static_cast<double>(channel[n]) - expected));
            }
        }
        CHECK(worst < 1e-7);
    };
    if (double_precision) {
        check(0.0);
    } else {
        check(0.f);
    }
}

TEST_CASE("latency follows lookahead and oversampling", "[latency]") {
    PluginProcessor p;
    prepare(p);
    runSine(p, -20.f, .6);
    pumpMessages();
    CHECK(p.getLatencySamples() == 0);

    set(p, zlp::PLookAhead::kID, 5.f);
    runSine(p, -20.f, .6);
    pumpMessages();
    const auto lookahead = p.getLatencySamples();
    CHECK(std::abs(lookahead - 240) <= 1);

    set(p, zlp::POversample::kID, 1.f);
    runSine(p, -20.f, .6);
    pumpMessages();
    CHECK(p.getLatencySamples() > lookahead);

    // a negative lookahead delays the side chain instead, which costs no latency
    set(p, zlp::POversample::kID, 0.f);
    set(p, zlp::PLookAhead::kID, -5.f);
    runSine(p, -20.f, .6);
    pumpMessages();
    CHECK(p.getLatencySamples() == 0);
}

TEST_CASE("host blocks larger than the prepared size are processed in pieces", "[realtime]") {
    // JUCE allows a host to send more than prepareToPlay announced; the engine's buffers only hold that much
    const auto double_precision = GENERATE(false, true);
    CAPTURE(double_precision);
    const auto render = [&]<typename T>(T, const int host_block) {
        PluginProcessor p;
        makeBusy(p);
        prepare(p, 256);
        return run<T>(p, {bursts(), silence(), 0, false, {host_block}}, seconds(1.0));
    };
    if (double_precision) {
        CHECK(worstDifference(render(0.0, 256), render(0.0, 1500)) < 1e-5f);
    } else {
        CHECK(worstDifference(render(0.f, 256), render(0.f, 1500)) < 1e-5f);
    }
}

TEST_CASE("the output does not depend on the block size", "[realtime]") {
    const auto render = [](const std::vector<int>& blocks) {
        PluginProcessor p;
        makeBusy(p);
        prepare(p);
        return run(p, {bursts(), silence(), 0, false, blocks}, seconds(1.0));
    };
    // the engine's vector code and its scalar tail round differently, so allow for the last bits
    CHECK(worstDifference(render({kBlock}), render({1, 7, 64, 333, 512, 31})) < 1e-5f);
}

TEST_CASE("the audio thread does not allocate", "[realtime]") {
    const auto double_precision = GENERATE(false, true);
    CAPTURE(double_precision);
    PluginProcessor p;
    makeBusy(p);
    prepare(p);
    const auto measure = [&]<typename T>(T) {
        juce::AudioBuffer<T> buffer(4, kBlock);
        juce::MidiBuffer midi;
        const auto signal = bursts();
        size_t allocations = 0;
        for (int b = 0; b < 400; ++b) {
            for (int c = 0; c < 4; ++c) {
                for (int i = 0; i < kBlock; ++i) {
                    buffer.setSample(c, i, static_cast<T>(signal(b * kBlock + i)));
                }
            }
            // settings change on the way, as automation would
            if (b == 100) {
                set(p, zlp::PLookAhead::kID, 8.f);
                set(p, zlp::PCompStyle::kID, 2.f);
                set(p, zlp::PFilterStatus::kID + juce::String(1), 2.f);
                set(p, zlp::PSideOut::kID, 1.f);
            }
            if (b == 200) {
                set(p, zlp::PSideOut::kID, 0.f);
                set(p, zlp::PCompDirection::kID, 2.f);
                p.getEqualizeController().setSoloBand(0);
            }
            if (b == 300) {
                p.getEqualizeController().setSoloBand(zlp::kBandNum);
                set(p, zlp::PCompDelta::kID, 1.f);
            }
            const alloc_counter::Scope scope;
            if (b % 50 == 49) {
                p.processBlockBypassed(buffer, midi);
            } else {
                p.processBlock(buffer, midi);
            }
            allocations += scope.count();
        }
        CHECK(allocations == 0);
    };
    if (double_precision) {
        measure(0.0);
    } else {
        measure(0.f);
    }
}

TEST_CASE("listening to a stereo side chain on a mono track", "[routing]") {
    // mono main, stereo aux: the output hears the mid of the external side chain
    PluginProcessor p;
    setLayout(p, 1, 2);
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
