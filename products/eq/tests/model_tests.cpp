// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include <numbers>

#include "PluginProcessor.hpp"
#include "ui/model/engine_bridge.hpp"
#include "ui/model/band_ops.hpp"
#include "ui/analysis/response_model.hpp"

using namespace contour::ui;
using FT = zldsp::filter::FilterType;


namespace {
    void setPlain(PluginProcessor& p, const juce::String& id, const float v) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(v));
    }

    /**
     * magnitude of the processor's impulse response at a frequency, in dB
     */
    double measuredDB(const std::vector<float>& ir, const double freq, const double sample_rate) {
        double re = 0.0, im = 0.0;
        const auto w = 2.0 * std::numbers::pi * freq / sample_rate;
        for (size_t n = 0; n < ir.size(); ++n) {
            re += ir[n] * std::cos(w * static_cast<double>(n));
            im -= ir[n] * std::sin(w * static_cast<double>(n));
        }
        return 10.0 * std::log10(re * re + im * im + 1e-30);
    }

    std::vector<float> impulseResponse(PluginProcessor& p, const double sample_rate, const int length) {
        constexpr int kBlock = 256;
        juce::AudioBuffer<float> buffer(4, kBlock);
        juce::MidiBuffer midi;
        // let parameter smoothing settle
        for (int i = 0; i < static_cast<int>(sample_rate) / kBlock; ++i) {
            buffer.clear();
            p.processBlock(buffer, midi);
        }
        std::vector<float> ir;
        ir.reserve(static_cast<size_t>(length));
        for (int pos = 0; pos < length; pos += kBlock) {
            buffer.clear();
            if (pos == 0) {
                buffer.setSample(0, 0, 1.f);
                buffer.setSample(1, 0, 1.f);
            }
            p.processBlock(buffer, midi);
            for (int i = 0; i < kBlock; ++i) {
                ir.push_back(buffer.getSample(0, i));
            }
        }
        return ir;
    }
}

namespace {
    void checkStructure(const zlp::FilterStructure structure, const double sample_rate, const double tolerance) {
    const auto kSampleRate = sample_rate;
    PluginProcessor p;
    p.setRateAndBufferSizeDetails(kSampleRate, 256);
    p.prepareToPlay(kSampleRate, 256);

    struct Config {
        FT type;
        float freq, gain, q;
        int order_idx;
    };
    const std::vector<Config> configs{
        {FT::kPeak, 250.f, 6.f, 1.2f, 1},
        {FT::kPeak, 3000.f, -9.f, 4.f, 1},
        {FT::kLowShelf, 120.f, 4.f, .707f, 1},
        {FT::kHighShelf, 8000.f, -5.f, .707f, 1},
        {FT::kHighPass, 40.f, 0.f, .707f, 2},
        {FT::kLowPass, 15000.f, 0.f, .9f, 3},
    };
    for (size_t i = 0; i < configs.size(); ++i) {
        const auto s = juce::String(i);
        setPlain(p, zlp::PFilterType::kID + s, static_cast<float>(configs[i].type));
        setPlain(p, zlp::POrder::kID + s, static_cast<float>(configs[i].order_idx));
        setPlain(p, zlp::PFreq::kID + s, configs[i].freq);
        setPlain(p, zlp::PGain::kID + s, configs[i].gain);
        setPlain(p, zlp::PQ::kID + s, configs[i].q);
        setPlain(p, zlp::PFilterStatus::kID + s, static_cast<float>(zlp::kOn));
    }

    setPlain(p, zlp::PFilterStructure::kID, static_cast<float>(structure));
    const auto ir = impulseResponse(p, kSampleRate, 1 << 16);

    EngineBridge bridge(p);
    bridge.refresh();
    ResponseModel model;
    model.setNumPoints(400);
    model.update(bridge, 1.0, true);
    const auto& sum = model.getSumDB(0);

    double worst = 0.0, worst_freq = 0.0;
    for (size_t i = 0; i < model.getNumPoints(); ++i) {
        const auto f = model.getFreq(i);
        if (f < 20.0 || f > 20000.0) {
            continue;
        }
        const auto err = std::abs(measuredDB(ir, f, kSampleRate) - static_cast<double>(sum[i]));
        if (std::getenv("CONTOUR_TEST_VERBOSE") != nullptr && i % 8 == 0) {
            std::cout << f << " Hz  drawn " << sum[i] << "  measured " << measuredDB(ir, f, kSampleRate) << std::endl;
        }
        if (err > worst) {
            worst = err;
            worst_freq = f;
        }
    }
    INFO(zlp::PFilterStructure::kChoices[static_cast<int>(structure)] << " @ " << sample_rate
         << ": worst deviation " << worst << " dB at " << worst_freq << " Hz");
    CHECK(worst < tolerance);
    std::cout << "response accuracy, " << zlp::PFilterStructure::kChoices[static_cast<int>(structure)] << " @ "
        << sample_rate << " Hz: worst deviation " << worst << " dB at " << worst_freq << " Hz" << std::endl;
    }
}

TEST_CASE("drawn response matches the processed audio", "[response]") {
    for (const auto sr : {48000.0, 96000.0}) {
        checkStructure(zlp::kMinimum, sr, .05);
        checkStructure(zlp::kSVF, sr, .05);
        checkStructure(zlp::kParallel, sr, .05);
        checkStructure(zlp::kMatched, sr, .25);
        checkStructure(zlp::kMixed, sr, .3);
        // the linear-phase FIR has finite resolution in steep low-frequency slopes
        checkStructure(zlp::kZero, sr, .3);
    }
}

TEST_CASE("band operations are single undo steps", "[bands]") {
    PluginProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 256);
    p.prepareToPlay(48000.0, 256);
    EngineBridge bridge(p);

    const auto first = band_ops::add(bridge, 1000.0, 3.f, FT::kPeak);
    bridge.refresh();
    REQUIRE(first == 0);
    CHECK(bridge.band(0).isOn());
    CHECK(std::abs(bridge.band(0).gain - 3.f) < 1e-3f);

    const auto second = band_ops::add(bridge, 60.0, 0.f, FT::kHighPass);
    bridge.refresh();
    CHECK(second == 1);
    CHECK(bridge.band(1).order_idx == 2);

    band_ops::remove(bridge, {0});
    bridge.refresh();
    CHECK_FALSE(bridge.band(0).isActive());
    // a freed slot is reused, with every parameter back at its default
    setPlain(p, zlp::PDynamicON::kID + juce::String(0), 1.f);
    const auto reused = band_ops::add(bridge, 500.0, -2.f, FT::kPeak);
    bridge.refresh();
    CHECK(reused == 0);
    CHECK_FALSE(bridge.band(0).dynamic_on);

    auto& um = bridge.getUndoManager();
    um.undo();
    bridge.refresh();
    CHECK_FALSE(bridge.band(0).isActive());
    um.undo();
    um.undo();
    bridge.refresh();
    CHECK(bridge.band(0).isActive());
    CHECK_FALSE(bridge.band(1).isActive());
    um.redo();
    bridge.refresh();
    CHECK(bridge.band(1).isActive());
}

TEST_CASE("node position mapping for shelves", "[bands]") {
    BandSnapshot b;
    b.type = FT::kLowShelf;
    b.gain = 8.f;
    CHECK(band_ops::getNodeDB(b) == 4.f);
    CHECK(band_ops::nodeDBToGain(b, 4.f) == 8.f);
    b.type = FT::kPeak;
    CHECK(band_ops::getNodeDB(b) == 8.f);
    b.type = FT::kHighPass;
    CHECK(band_ops::getNodeDB(b) == 0.f);
}
