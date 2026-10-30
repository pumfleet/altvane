// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include <complex>
#include <numbers>

#include "PluginProcessor.hpp"
#include "ui/model/engine_bridge.hpp"
#include "ui/model/band_ops.hpp"
#include "ui/analysis/response_model.hpp"

using namespace altvane::eq;
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
        // e^{-jwn} by rotation: a sin and a cos per sample would dominate the run time of the long responses below
        const auto w = 2.0 * std::numbers::pi * freq / sample_rate;
        const std::complex<double> step{std::cos(w), -std::sin(w)};
        std::complex<double> phasor{1.0, 0.0}, sum{0.0, 0.0};
        for (size_t n = 0; n < ir.size(); ++n) {
            sum += static_cast<double>(ir[n]) * phasor;
            phasor *= step;
            if ((n & 1023) == 1023) {
                phasor /= std::abs(phasor);
            }
        }
        return 10.0 * std::log10(std::norm(sum) + 1e-30);
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

    struct Config {
        FT type;
        float freq, gain, q;
        int order_idx;
    };

    const std::vector<Config> kMix{
        {FT::kPeak, 250.f, 6.f, 1.2f, 1},
        {FT::kPeak, 3000.f, -9.f, 4.f, 1},
        {FT::kLowShelf, 120.f, 4.f, .707f, 1},
        {FT::kHighShelf, 8000.f, -5.f, .707f, 1},
        {FT::kHighPass, 40.f, 0.f, .707f, 2},
        {FT::kLowPass, 15000.f, 0.f, .9f, 3},
    };

    /**
     * settings harder to draw than the mix: every other shape, the steepest slopes, bands at and beyond Nyquist (which
     * the engine designs as they are), extreme gains and Qs, and stacked low-frequency bands whose poles sit close to
     * z = 1 at high sample rates
     */
    const std::vector<std::pair<const char*, std::vector<Config>>> kHard{
        {"shapes", {{FT::kNotch, 1000.f, 0.f, 8.f, 1}, {FT::kBandPass, 500.f, 0.f, 2.f, 1},
                    {FT::kTiltShelf, 2000.f, 6.f, .707f, 1}, {FT::kAllPass, 3000.f, 0.f, 1.f, 1},
                    {FT::kFlatTilt, 1000.f, 4.f, .707f, 1}, {FT::kFlatGain, 1000.f, -2.f, .707f, 1}}},
        {"steep", {{FT::kHighPass, 80.f, 0.f, .707f, 6}, {FT::kLowPass, 12000.f, 0.f, .707f, 6},
                   {FT::kLowShelf, 300.f, 6.f, .707f, 4}, {FT::kHighShelf, 5000.f, -6.f, .707f, 5},
                   {FT::kPeak, 1500.f, 8.f, 2.f, 3}}},
        {"high", {{FT::kHighShelf, 18000.f, 6.f, .707f, 1}, {FT::kPeak, 19000.f, -10.f, 6.f, 1},
                  {FT::kPeak, 30000.f, 6.f, .707f, 1}, {FT::kHighShelf, 25000.f, -4.f, .707f, 1}}},
        {"extreme", {{FT::kPeak, 100.f, 12.f, 25.f, 1}, {FT::kPeak, 10000.f, -15.f, .1f, 1},
                     {FT::kPeak, 600.f, 30.f, 3.f, 1}, {FT::kPeak, 6000.f, -30.f, 10.f, 1}}},
        {"low end", {{FT::kHighPass, 20.f, 0.f, .707f, 6}, {FT::kLowShelf, 25.f, 12.f, .707f, 1}}},
    };

    /**
     * the worst difference between the drawn sum and the processed audio from 20 Hz to 20 kHz, in dB
     */
    double worstDeviation(const std::vector<Config>& configs, const zlp::FilterStructure structure,
                          const double sample_rate, const int length, double& worst_freq) {
        PluginProcessor p;
        p.setRateAndBufferSizeDetails(sample_rate, 256);
        p.prepareToPlay(sample_rate, 256);
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
        const auto ir = impulseResponse(p, sample_rate, length);

        EngineBridge bridge(p);
        bridge.refresh();
        ResponseModel model;
        model.setNumPoints(400);
        model.update(bridge, 1.0, true);
        const auto& sum = model.getSumDB(0);

        double worst = 0.0;
        for (size_t i = 0; i < model.getNumPoints(); ++i) {
            const auto f = model.getFreq(i);
            // below -90 dB the float impulse response is too coarse to resolve a hundredth of a dB
            if (f < 20.0 || f > 20000.0 || sum[i] < -90.f) {
                continue;
            }
            const auto measured = measuredDB(ir, f, sample_rate);
            if (std::getenv("ALTVANE_TEST_VERBOSE") != nullptr && i % 8 == 0) {
                std::cout << f << " Hz  drawn " << sum[i] << "  measured " << measured << std::endl;
            }
            if (const auto err = std::abs(measured - static_cast<double>(sum[i])); err > worst) {
                worst = err;
                worst_freq = f;
            }
        }
        return worst;
    }

    void checkStructure(const char* name, const std::vector<Config>& configs, const zlp::FilterStructure structure,
                        const double sample_rate, const double tolerance, const int length = 1 << 16) {
        double worst_freq = 0.0;
        const auto worst = worstDeviation(configs, structure, sample_rate, length, worst_freq);
        const auto& structure_name = zlp::PFilterStructure::kChoices[static_cast<int>(structure)];
        INFO(name << ", " << structure_name << " @ " << sample_rate << ": worst deviation " << worst << " dB at "
             << worst_freq << " Hz");
        CHECK(worst < tolerance);
        std::cout << "response accuracy, " << name << ", " << structure_name << " @ " << sample_rate
            << " Hz: worst deviation " << worst << " dB at " << worst_freq << " Hz" << std::endl;
    }
}

TEST_CASE("drawn response matches the processed audio", "[response]") {
    for (const auto sr : {44100.0, 48000.0, 96000.0}) {
        // the zero-latency structures run the very filters drawn: the README promises 0.01 dB
        for (const auto structure : {zlp::kMinimum, zlp::kSVF, zlp::kParallel}) {
            checkStructure("mix", kMix, structure, sr, .01);
            for (const auto& [name, configs] : kHard) {
                // the 96 dB/oct high-pass at 20 Hz rings for seconds
                checkStructure(name, configs, structure, sr, .01, 1 << 18);
            }
        }
        checkStructure("mix", kMix, zlp::kMatched, sr, .25);
        checkStructure("mix", kMix, zlp::kMixed, sr, .3);
        // the linear-phase FIR has finite resolution in steep low-frequency slopes
        checkStructure("mix", kMix, zlp::kZero, sr, .3);
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
