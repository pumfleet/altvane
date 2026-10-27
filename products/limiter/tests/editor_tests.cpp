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
#include "ui/editor.hpp"
#include "ui/factory_presets.hpp"
#include "ui/model/level_feed.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;
using namespace altvane::limiter;

namespace {
    struct Fixture {
        PluginProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor{nullptr};
        double t{1.0};

        Fixture() {
            p.setPlayConfigDetails(2, 2, kSampleRate, kBlock);
            p.prepareToPlay(kSampleRate, kBlock);
            base.reset(p.createEditor());
            editor = dynamic_cast<Editor*>(base.get());
            REQUIRE(editor != nullptr);
            frame();
        }

        ~Fixture() {
            base.reset();
            p.releaseResources();
        }

        void frame() {
            t += 1.0 / 60.0;
            editor->runFrameForTesting(t);
        }

        float raw(const char* id) { return p.parameters_.getRawParameterValue(id)->load(); }
    };
}

TEST_CASE("the gain knob is an accessible slider with one undo step and the host's menu", "[editor]") {
    Fixture f;
    auto& knob = f.editor->getCardForTesting().getGainKnobForTesting();
    CHECK(f.editor->getControlParameterIndex(knob) ==
          f.p.parameters_.getParameter(zlp::PInputGain::kID)->getParameterIndex());
    const auto handler = knob.createAccessibilityHandler();
    REQUIRE(handler != nullptr);
    CHECK(handler->getRole() == juce::AccessibilityRole::slider);
    REQUIRE(handler->getValueInterface() != nullptr);
    handler->getValueInterface()->setValueAsString("6");
    CHECK_THAT(f.raw(zlp::PInputGain::kID), WithinAbs(6.0, .01));
    // help for sighted and screen-reader users alike
    CHECK(knob.getHelpText().isNotEmpty());

    auto& undo = f.p.session_.undo_manager;
    REQUIRE(undo.canUndo());
    undo.undo();
    CHECK_THAT(f.raw(zlp::PInputGain::kID), WithinAbs(0.0, .01));
}

TEST_CASE("a character sets the timing in one step, and reads Custom once changed", "[editor]") {
    Fixture f;
    auto& card = f.editor->getCardForTesting();
    card.tick();
    CHECK(card.getCharacterName() == "Balanced");

    card.applyCharacter(2);
    card.tick();
    CHECK(card.getCharacterName() == "Punchy");
    CHECK_THAT(f.raw(zlp::PLookahead::kID), WithinAbs(kCharacters[2].lookahead, .01));
    CHECK_THAT(f.raw(zlp::PRecovery::kID), WithinAbs(kCharacters[2].recovery, .05));
    CHECK_THAT(f.raw(zlp::PChannelDelta::kID), WithinAbs(kCharacters[2].channel_delta, .01));

    set(f.p, zlp::PAttack::kID, 45.f);
    card.tick();
    CHECK(card.getCharacterName() == "Custom");

    // the character was one undo step
    set(f.p, zlp::PAttack::kID, kCharacters[2].attack);
    f.p.session_.undo_manager.undo();
    card.tick();
    CHECK(card.getCharacterName() == "Balanced");
}

TEST_CASE("the display drags or nudges the gain, and offers the ceiling near its line", "[editor]") {
    Fixture f;
    auto& history = f.editor->getHistoryForTesting();
    f.frame();
    // away from the ceiling line, the display stands for the gain
    CHECK(f.editor->getControlParameterIndex(history) ==
          f.p.parameters_.getParameter(zlp::PInputGain::kID)->getParameterIndex());
    REQUIRE(history.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    f.frame();
    CHECK_THAT(f.raw(zlp::PInputGain::kID), WithinAbs(.5, .01));

    // the ceiling line sits where the ceiling is
    const auto plot = history.getPlotArea();
    CHECK_THAT(history.yOfDB(0.f), WithinAbs(plot.getY(), 1e-4));
    CHECK(history.yOfDB(-.1f) > plot.getY());
}

TEST_CASE("gain to target moves the gain by the loudness difference and starts over", "[editor]") {
    Fixture f;
    set(f.p.na_parameters_, zlstate::PLoudnessTarget::kID, 0.f); // streaming, -14 LUFS
    const auto signal = sine(1000.0, -23.f);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < static_cast<int>(4.0 * kSampleRate / kBlock); ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const auto v = signal(b * kBlock + i).first;
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        f.p.processBlock(buffer, midi);
        if (b % 8 == 0) {
            f.frame();
        }
    }
    f.frame();
    auto& panel = f.editor->getLoudnessForTesting();
    const auto delta = panel.getGainToTarget();
    REQUIRE(delta.has_value());
    CHECK_THAT(*delta, WithinAbs(9.0, .2));

    const auto generation = f.p.pullLoudness().generation;
    panel.matchTarget();
    CHECK_THAT(f.raw(zlp::PInputGain::kID), WithinAbs(9.0, .2));
    f.p.processBlock(buffer, midi);
    CHECK(f.p.pullLoudness().generation > generation);
    // one undo step
    f.p.session_.undo_manager.undo();
    CHECK_THAT(f.raw(zlp::PInputGain::kID), WithinAbs(0.0, .01));
}

TEST_CASE("factory presets load, and keep the session's display", "[presets]") {
    Fixture f;
    const auto codec = presetCodec(f.p);
    set(f.p.na_parameters_, zlstate::PAnalyzerTimeLength::kID, 3.f);
    const auto presets = factoryPresets(f.p);
    REQUIRE(presets.size() >= 8);
    for (const auto& [path, text] : presets) {
        INFO(path);
        const auto file = juce::File::createTempFile(".json");
        REQUIRE(file.replaceWithText(text));
        juce::MemoryBlock state;
        REQUIRE(codec.read(file, state).wasOk());
        f.p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        file.deleteFile();
        CHECK(f.p.na_parameters_.getRawParameterValue(zlstate::PAnalyzerTimeLength::kID)->load() == 3.f);
        if (path.endsWith("Broadcast EBU R128")) {
            CHECK(f.p.na_parameters_.getRawParameterValue(zlstate::PLoudnessTarget::kID)->load() == 3.f);
            f.editor->getCardForTesting().tick();
            CHECK(f.editor->getCardForTesting().getCharacterName() == "Safe");
        }
        if (path.endsWith("CD Master 16 bit")) {
            CHECK(f.raw(params::PDitherBits::kID) == 3.f);
        }
    }
}

TEST_CASE("panels open and close with the session", "[editor]") {
    Fixture f;
    f.editor->setLoudnessOpen(false);
    f.editor->setControlsOpen(false);
    f.frame();
    CHECK_FALSE(f.editor->getLoudnessForTesting().isVisible());
    CHECK_FALSE(f.editor->getCardForTesting().isVisible());
    juce::MemoryBlock state;
    f.p.getStateInformation(state);
    PluginProcessor other;
    other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(other.na_parameters_.getRawParameterValue(zlstate::PLoudnessPanelOpen::kID)->load() < .5f);

    set(f.p.na_parameters_, zlstate::PLoudnessPanelOpen::kID, 1.f);
    f.frame();
    CHECK(f.editor->getLoudnessForTesting().isVisible());
}

TEST_CASE("match target waits for bypass, audition and unity gain to be off", "[editor]") {
    Fixture f;
    set(f.p.na_parameters_, zlstate::PLoudnessTarget::kID, 0.f);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    const auto signal = sine(1000.0, -23.f);
    for (int b = 0; b < static_cast<int>(3.0 * kSampleRate / kBlock); ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const auto v = signal(b * kBlock + i).first;
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        f.p.processBlock(buffer, midi);
    }
    auto& panel = f.editor->getLoudnessForTesting();
    // bypassed the meter hears the dry input, in Audition what the limiter removes, and unity gain gives the gain
    // back: in each, moving the gain would not move the loudness
    for (const auto* id : {zlp::PBypass::kID, zlp::PDelta::kID, zlp::PInputOutputLink::kID}) {
        CAPTURE(id);
        set(f.p, id, 1.f);
        f.frame();
        CHECK(panel.getMatchBlocker().has_value());
        CHECK_FALSE(panel.getGainToTarget().has_value());
        panel.matchTarget();
        CHECK_THAT(f.raw(zlp::PInputGain::kID), WithinAbs(0.0, .01));
        set(f.p, id, 0.f);
        f.frame();
        CHECK_FALSE(panel.getMatchBlocker().has_value());
        CHECK(panel.getGainToTarget().has_value());
    }
    // and the host's bypass, which Altvane learns from the blocks the host sends
    f.p.processBlockBypassed(buffer, midi);
    f.frame();
    CHECK(panel.getMatchBlocker().has_value());
    f.p.processBlock(buffer, midi);
    f.frame();
    CHECK_FALSE(panel.getMatchBlocker().has_value());
}

TEST_CASE("the display's output is what is heard, in bypass and in audition", "[editor]") {
    // the output peaks the display and meters get for a steady sine, with some parameters set
    const auto output_peak = [](const Generator& signal, const std::vector<std::pair<const char*, float>>& values) {
        PluginProcessor p;
        for (const auto& [id, v] : values) {
            set(p, id, v);
        }
        prepare(p);
        LevelFeed feed(p);
        feed.setActive(true);
        feed.pull(LoudnessReadings::kNoLevel, false);
        for (int i = 0; i < 4; ++i) {
            run(p, signal, .2);
            feed.pull(LoudnessReadings::kNoLevel, false);
        }
        feed.setActive(false);
        return std::max(feed.output_peaks[0], feed.output_peaks[1]);
    };
    const std::pair<const char*, float> push{zlp::PInputGain::kID, 12.f}, ceiling{zlp::POutputCeiling::kID, -1.f};
    // limiting: the output sits at the ceiling
    CHECK_THAT(output_peak(sine(1000.0, -6.f), {push, ceiling}), WithinAbs(-1.0, .2));
    // bypassed: the dry input
    CHECK_THAT(output_peak(sine(1000.0, -20.f), {push, ceiling, {zlp::PBypass::kID, 1.f}}), WithinAbs(-20.0, .2));
    // audition: only what the limiter removes, so nothing from a signal it leaves alone...
    CHECK(output_peak(sine(1000.0, -30.f), {{zlp::PDelta::kID, 1.f}}) < -80.f);
    // ...and something from one it limits
    CHECK(output_peak(sine(1000.0, -6.f), {push, ceiling, {zlp::PDelta::kID, 1.f}}) > -20.f);
}

TEST_CASE("the window shrinks to its minimum without clipping or stale history", "[editor]") {
    Fixture f;
    f.editor->setSize(tokens::size::kDefaultWidth, tokens::size::kDefaultHeight);
    // fill the history: the analyzer runs while the editor shows, so start it by hand
    f.p.getController().setAnalyzerEnabled(true);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    const auto signal = sine(220.0, -3.f);
    for (int b = 0; b < static_cast<int>(3.0 * kSampleRate / kBlock); ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const auto v = signal(b * kBlock + i).first;
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        f.p.processBlock(buffer, midi);
        f.frame();
    }
    auto& history = f.editor->getHistoryForTesting();
    REQUIRE(history.getPixelCountForTesting() > 100);

    // no frame in between: the pixel columns follow the new width straight away
    f.editor->setSize(tokens::size::kMinWidth, tokens::size::kMinHeight);
    CHECK(static_cast<float>(history.getPixelCountForTesting()) <= std::floor(history.getPlotArea().getWidth()));

    // every control of the card stays inside it
    auto& card = f.editor->getCardForTesting();
    CHECK(card.isVisible());
    for (auto* child : card.getChildren()) {
        CAPTURE(child->getTitle(), child->getBounds().toString(), card.getLocalBounds().toString());
        CHECK(card.getLocalBounds().contains(child->getBounds()));
    }
    f.p.getController().setAnalyzerEnabled(false);
}
