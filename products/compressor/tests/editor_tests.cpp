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

#include "PluginProcessor.hpp"
#include "ui/editor.hpp"

using Catch::Matchers::WithinAbs;

namespace {
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 512;

    void set(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* param = p.parameters_.getParameter(id);
        REQUIRE(param != nullptr);
        param->setValueNotifyingHost(param->convertTo0to1(plain));
    }

    /**
     * a 1 kHz sine at a steady peak level; returns the output peak (dBFS) over the last half second
     */
    float runSine(PluginProcessor& p, const float peak_db, const double seconds) {
        juce::AudioBuffer<float> buffer(4, kBlock);
        juce::MidiBuffer midi;
        const auto amp = juce::Decibels::decibelsToGain(peak_db);
        const auto blocks = static_cast<int>(seconds * kSampleRate / kBlock);
        const auto measure_from = blocks - static_cast<int>(.5 * kSampleRate / kBlock);
        double phase = 0.0;
        float out_peak = 0.f;
        for (int b = 0; b < blocks; ++b) {
            buffer.clear();
            for (int i = 0; i < kBlock; ++i) {
                const auto v = amp * static_cast<float>(std::sin(phase));
                phase += juce::MathConstants<double>::twoPi * 1000.0 / kSampleRate;
                buffer.setSample(0, i, v);
                buffer.setSample(1, i, v);
            }
            p.processBlock(buffer, midi);
            if (b >= measure_from) {
                out_peak = std::max(out_peak, buffer.getMagnitude(0, 0, kBlock));
            }
        }
        return juce::Decibels::gainToDecibels(out_peak, -240.f);
    }

    /**
     * a static setting: linked left / right peak detection, instant attack, slow release,
     * so that a steady sine settles at the curve's gain for its peak level
     */
    void makeStatic(PluginProcessor& p) {
        set(p, zlp::PSideStereoMode::kID, 1.f);
        set(p, zlp::PSideStereoLink::kID, 100.f);
        set(p, zlp::PAttack::kID, 0.f);
        set(p, zlp::PRelease::kID, 2000.f);
    }

    void collect(juce::Component& c, std::vector<juce::Component*>& out) {
        for (auto* child : c.getChildren()) {
            out.push_back(child);
            collect(*child, out);
        }
    }
}

TEST_CASE("the knee display draws the engine's static curve", "[editor][curve]") {
    const auto direction = GENERATE(0, 1, 2, 3);
    const auto level = GENERATE(-50.f, -38.f, -30.f, -22.f, -10.f);
    CAPTURE(direction, level);

    PluginProcessor p;
    makeStatic(p);
    set(p, zlp::PCompDirection::kID, static_cast<float>(direction));
    // expanding and inflating act more as the level falls, so a sine's zero crossings would drive a peak detector;
    // measure them on the RMS detector instead, which sees a steady sine 3 dB below its peak
    const auto rms = direction == zlp::PCompDirection::kInflate || direction == zlp::PCompDirection::kExpand;
    if (rms) {
        set(p, zlp::PRMSON::kID, 1.f);
        set(p, zlp::PRMSMix::kID, 100.f);
        set(p, zlp::PAttack::kID, 100.f);
        set(p, zlp::PRelease::kID, 100.f);
    }
    set(p, zlp::PThreshold::kID, -30.f);
    set(p, zlp::PRatio::kID, 3.f);
    set(p, zlp::PKneeW::kID, 4.f);
    set(p, zlp::PRange::kID, 12.f);
    set(p, zlp::POutGain::kID, 2.f);
    p.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    p.prepareToPlay(kSampleRate, kBlock);

    altvane::ui::ParamEditor editor(p.session_.undo_manager);
    altvane::compressor::CompBridge bridge(p, editor);
    altvane::compressor::TransferModel model;
    model.sync(bridge);

    const auto detected = rms ? level - 3.0103f : level;
    const auto expected = level + model.gainFor(detected) + model.getSettings().makeup;
    CHECK_THAT(runSine(p, level, 3.0), WithinAbs(expected, .5));
}

TEST_CASE("the level history measures input, output and gain reduction", "[editor][history]") {
    PluginProcessor p;
    makeStatic(p);
    set(p, zlp::PThreshold::kID, -30.f);
    set(p, zlp::PRatio::kID, 4.f);
    set(p, zlp::PKneeW::kID, 0.f);
    p.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    p.prepareToPlay(kSampleRate, kBlock);

    altvane::compressor::LevelHistory history(p);
    history.setGeometry(600, 6.f);
    // the history reads what arrives after it starts, a little at a time, as the editor does
    for (int i = 0; i < 40; ++i) {
        runSine(p, -10.f, .05);
        history.update();
    }
    REQUIRE(history.size() > 10);
    const auto& newest = history.get(0);
    CHECK_THAT(newest.input, WithinAbs(-10.0, .5));
    // 20 dB over the threshold at 4:1: 15 dB of reduction
    CHECK_THAT(newest.gain, WithinAbs(-15.0, .5));
    CHECK_THAT(newest.output, WithinAbs(-25.0, .5));

    const auto meters = history.takeMeters();
    CHECK(meters.fresh);
    CHECK_THAT(meters.input[0], WithinAbs(-10.0, .5));
    CHECK_THAT(meters.gain, WithinAbs(-15.0, .5));
}

TEST_CASE("every control maps to its host parameter", "[editor][accessibility]") {
    PluginProcessor p;
    p.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    p.prepareToPlay(kSampleRate, kBlock);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    REQUIRE(editor != nullptr);

    std::vector<juce::Component*> all;
    collect(*editor, all);
    std::set<juce::String> mapped;
    for (auto* c : all) {
        const auto id = c->getComponentID();
        if (auto* para = p.parameters_.getParameter(id)) {
            CHECK(editor->getControlParameterIndex(*c) == para->getParameterIndex());
            mapped.insert(id);
        }
    }
    for (const auto* id : {
             zlp::PThreshold::kID, zlp::PRatio::kID, zlp::PKneeW::kID, zlp::PRange::kID, zlp::PAttack::kID,
             zlp::PRelease::kID, zlp::PHold::kID, zlp::PLookAhead::kID, zlp::POutGain::kID, zlp::PWet::kID,
             zlp::PCompStyle::kID, zlp::PCompDirection::kID, zlp::POversample::kID, zlp::PCompDelta::kID,
             zlp::PCompON::kID, zlp::PRMSMix::kID, zlp::PClipperDrive::kID, zlp::PSideGain::kID,
             zlp::PSideStereoLink::kID, zlp::PExtSide::kID, zlp::PSideOut::kID
         }) {
        CAPTURE(id);
        CHECK(mapped.count(id) == 1);
    }
    p.editorBeingDeleted(editor.get());
}

TEST_CASE("the editor runs frames with audio flowing", "[editor]") {
    PluginProcessor p;
    p.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    p.prepareToPlay(kSampleRate, kBlock);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<altvane::compressor::Editor*>(base.get());
    REQUIRE(editor != nullptr);

    // open everything once: the side chain drawer, the advanced controls, then close again
    auto* drawer = p.na_parameters_.getParameter(zlstate::PSideControlDisplay::kID);
    double t = 0.0;
    for (int frame = 0; frame < 120; ++frame) {
        if (frame == 30) {
            drawer->setValueNotifyingHost(1.f);
            editor->setAdvancedOpen(true);
        }
        if (frame == 80) {
            drawer->setValueNotifyingHost(0.f);
            editor->setAdvancedOpen(false);
        }
        runSine(p, -12.f, 1.0 / 60.0);
        t += 1.0 / 60.0;
        editor->runFrameForTesting(t);
    }
    CHECK(editor->getHistoryForTesting().size() > 0);
    p.editorBeingDeleted(base.get());
}
