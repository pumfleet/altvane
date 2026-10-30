// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "alloc_counter.hpp"
#include "test_helpers.hpp"

using Catch::Matchers::WithinAbs;
using FT = zldsp::filter::FilterType;
using namespace test;

namespace {
    constexpr std::array kStructures{
        zlp::kMinimum, zlp::kSVF, zlp::kParallel, zlp::kMatched, zlp::kMixed, zlp::kZero
    };

    const char* name(const zlp::FilterStructure structure) {
        return zlp::PFilterStructure::kChoices[static_cast<int>(structure)].toRawUTF8();
    }

    void band(PluginProcessor& p, const size_t b, const FT type, const float freq, const float gain, const float q,
              const zlp::FilterStereo stereo = zlp::kStereo) {
        setBand(p, b, zlp::PFilterType::kID, static_cast<float>(type));
        setBand(p, b, zlp::PFreq::kID, freq);
        setBand(p, b, zlp::PGain::kID, gain);
        setBand(p, b, zlp::PQ::kID, q);
        setBand(p, b, zlp::PLRMode::kID, static_cast<float>(stereo));
        setBand(p, b, zlp::PFilterStatus::kID, static_cast<float>(zlp::kOn));
    }

    /**
     * a busy mix-bus setting: a steep high-pass, cuts and boosts in stereo, left, right, mid and side, and a dynamic
     * band keyed from the side chain
     */
    void configure(PluginProcessor& p) {
        band(p, 0, FT::kHighPass, 30.f, 0.f, .707f);
        setBand(p, 0, zlp::POrder::kID, 3.f);
        band(p, 1, FT::kPeak, 250.f, -4.f, 1.2f, zlp::kMid);
        band(p, 2, FT::kHighShelf, 8000.f, 3.f, .707f, zlp::kSide);
        band(p, 3, FT::kPeak, 1800.f, 5.f, 4.f, zlp::kLeft);
        band(p, 4, FT::kLowShelf, 120.f, 2.f, .707f, zlp::kRight);
        band(p, 5, FT::kPeak, 3150.f, 0.f, 2.f);
        setBand(p, 5, zlp::PTargetGain::kID, -8.f);
        setBand(p, 5, zlp::PThreshold::kID, -30.f);
        setBand(p, 5, zlp::PDynamicON::kID, 1.f);
    }
}

TEST_CASE("host blocks larger than the prepared size are processed in pieces", "[realtime]") {
    // JUCE allows a host to send more than prepareToPlay announced; the processor's and the engine's buffers only
    // hold that much
    const auto render = [](const zlp::FilterStructure structure, const int host_block, const bool as_double) {
        PluginProcessor p;
        configure(p);
        set(p, zlp::PFilterStructure::kID, static_cast<float>(structure));
        set(p, zlp::PLookahead::kID, 3.f);
        prepare(p, 256);
        return as_double ? run<double>(p, music(), 1.0, {host_block}) : run<float>(p, music(), 1.0, {host_block});
    };
    for (const auto structure : {zlp::kMinimum, zlp::kParallel, zlp::kZero}) {
        for (const auto as_double : {false, true}) {
            INFO(name(structure) << (as_double ? ", double" : ", float"));
            CHECK(worstDifference(render(structure, 256, as_double), render(structure, 1500, as_double)) < 1e-6f);
        }
    }
}

TEST_CASE("the output does not depend on the block size", "[realtime]") {
    for (const auto structure : kStructures) {
        const auto render = [&](const std::vector<int>& blocks) {
            PluginProcessor p;
            configure(p);
            set(p, zlp::PFilterStructure::kID, static_cast<float>(structure));
            prepare(p, 1024);
            return run(p, music(), 1.0, blocks);
        };
        INFO(name(structure));
        CHECK(worstDifference(render({512}), render({1, 7, 64, 333, 1024, 31})) < 1e-6f);
    }
}

TEST_CASE("bypass passes the input, delayed by the reported latency", "[bypass]") {
    const auto input = music();
    for (const auto structure : kStructures) {
        for (const auto lookahead : {0.f, 5.f}) {
            for (const auto host : {false, true}) {
                PluginProcessor p;
                configure(p);
                // neither is part of the bypassed signal
                set(p, zlp::PPhaseFlip::kID, 1.f);
                set(p, zlp::POutputStereo::kID, static_cast<float>(zlp::kMid));
                set(p, zlp::POutputGain::kID, -6.f);
                set(p, zlp::PFilterStructure::kID, static_cast<float>(structure));
                set(p, zlp::PLookahead::kID, lookahead);
                if (!host) {
                    set(p, zlp::PBypass::kID, 1.f);
                }
                prepare(p);
                // the engine reports its latency from the message thread once it has processed a block
                run(p, [](int64_t) { return std::pair{0.f, 0.f}; }, .05, {kBlock}, host);
                pumpMessages();
                const auto latency = static_cast<int64_t>(p.getLatencySamples());
                const auto out = run(p, input, 1.0, {kBlock}, host);

                INFO(name(structure) << ", lookahead " << lookahead << " ms, " << (host ? "host" : "parameter")
                     << " bypass, latency " << latency);
                CHECK((latency > 0) == (lookahead > 0.f || structure >= zlp::kMatched));
                float worst = 0.f;
                for (size_t n = 0; n < out[0].size(); ++n) {
                    const auto t = static_cast<int64_t>(n) - latency;
                    const auto [l, r] = t >= 0 ? input(t) : std::pair{0.f, 0.f};
                    worst = std::max({worst, std::abs(out[0][n] - l), std::abs(out[1][n] - r)});
                }
                // the zero-latency structures copy the input; the FIR structures overlap-add it in floats
                CHECK(worst <= (structure >= zlp::kMatched ? 1e-6f : 0.f));
            }
        }
    }
}

TEST_CASE("a block never writes into the host's buffer from an earlier block", "[realtime]") {
    // the double-precision path processes the host's channels in place: nothing may keep pointing at them
    PluginProcessor p;
    configure(p);
    prepare(p);
    juce::MidiBuffer midi;
    juce::AudioBuffer<double> earlier(p.getTotalNumInputChannels(), kBlock);
    for (int c = 0; c < earlier.getNumChannels(); ++c) {
        juce::FloatVectorOperations::fill(earlier.getWritePointer(c), .25, kBlock);
    }
    p.processBlock(earlier, midi);
    // the host has moved on: whatever it keeps in that memory now is not the plug-in's
    for (int c = 0; c < earlier.getNumChannels(); ++c) {
        juce::FloatVectorOperations::fill(earlier.getWritePointer(c), 7.0, kBlock);
    }

    juce::AudioBuffer<float> next(p.getTotalNumInputChannels(), kBlock);
    for (int c = 0; c < next.getNumChannels(); ++c) {
        for (int i = 0; i < kBlock; ++i) {
            next.setSample(c, i, music()(i).first);
        }
    }
    p.processBlock(next, midi);
    for (int c = 0; c < earlier.getNumChannels(); ++c) {
        INFO("channel " << c);
        CHECK(juce::FloatVectorOperations::findMinAndMax(earlier.getReadPointer(c), kBlock) ==
              juce::Range<double>(7.0, 7.0));
    }
}

TEST_CASE("the audio thread does not allocate", "[realtime]") {
    PluginProcessor p;
    configure(p);
    setBand(p, 5, zlp::PDynamicLearn::kID, 1.f);
    setBand(p, 5, zlp::PDynamicRelative::kID, 1.f);
    set(p, zlp::PStaticGain::kID, 1.f);
    set(p, zlp::PAutoGain::kID, 1.f);
    set(p, zlp::PLookahead::kID, 3.f);
    prepare(p);
    // the analyzer and loudness match only run while the editor is open
    p.getController().setEditorON(true);
    p.getController().setLoudnessMatchON(true);

    juce::AudioBuffer<float> buffer(p.getTotalNumInputChannels(), kBlock);
    juce::MidiBuffer midi;
    const auto signal = music();
    int64_t position = 0;
    const auto process = [&] {
        for (int c = 0; c < buffer.getNumChannels(); ++c) {
            for (int i = 0; i < kBlock; ++i) {
                const auto [l, r] = signal(position + i);
                buffer.setSample(c, i, c % 2 == 0 ? l : r);
            }
        }
        position += kBlock;
        const alloc_counter::Scope scope;
        p.processBlock(buffer, midi);
        return scope.count();
    };

    for (const auto structure : kStructures) {
        // a change of structure reports the new latency through JUCE's message queue, which may allocate
        set(p, zlp::PFilterStructure::kID, static_cast<float>(structure));
        process();
        pumpMessages();
        size_t allocations = 0;
        for (int b = 0; b < 60; ++b) {
            // parameter changes between blocks: the engine picks them up in the next one
            setBand(p, 1, zlp::PGain::kID, -4.f + static_cast<float>(b % 9));
            setBand(p, 3, zlp::PFreq::kID, 1000.f + 50.f * static_cast<float>(b));
            setBand(p, 5, zlp::PThreshold::kID, -40.f + static_cast<float>(b % 20));
            if (b == 20) {
                p.getController().setSoloWholeIdx(1);
            } else if (b == 30) {
                p.getController().setSoloWholeIdx(zlp::kBandNum + 5);
            } else if (b == 40) {
                p.getController().setSoloWholeIdx(2 * zlp::kBandNum);
            }
            allocations += process();
        }
        INFO(name(structure));
        CHECK(allocations == 0);
    }
    p.getController().setEditorON(false);
}
