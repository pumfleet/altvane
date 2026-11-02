// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include "signals.hpp"
#include "ui/editor.hpp"
#include "ui/factory_presets.hpp"

using namespace test_signals;
using altvane::leveler::Editor;

namespace {
    struct Fixture {
        PluginProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor{nullptr};

        Fixture() {
            p.setRateAndBufferSizeDetails(48000.0, 480);
            p.prepareToPlay(48000.0, 480);
            base.reset(p.createEditor());
            editor = dynamic_cast<Editor*>(base.get());
            editor->runFrameForTesting(1.0);
        }

        ~Fixture() {
            base.reset();
            p.releaseResources();
        }
    };
}

TEST_CASE("the knobs are sliders that edit their parameter with one undo step each", "[editor]") {
    Fixture f;
    REQUIRE(f.editor != nullptr);
    auto& deck = f.editor->getDeckForTesting();
    struct Case {
        const char* id;
        const char* text;
        float expected;
    };
    for (const auto& c : {Case{params::kTarget, "-14", -14.f}, Case{params::kSpeed, "80", .8f},
                          Case{params::kSensitivity, "3", 3.f}, Case{params::kGuard, "25", .25f},
                          Case{params::kMusic, "40", .4f}}) {
        CAPTURE(c.id);
        auto& knob = deck.getKnob(c.id);
        // the host's parameter menu finds the parameter under the knob
        CHECK(f.editor->getControlParameterIndex(knob) == f.p.parameters_.getParameter(c.id)->getParameterIndex());
        const auto handler = knob.createAccessibilityHandler();
        REQUIRE(handler != nullptr);
        CHECK(handler->getRole() == juce::AccessibilityRole::slider);
        auto* value = handler->getValueInterface();
        REQUIRE(value != nullptr);
        value->setValueAsString(c.text);
        CHECK(std::abs(f.p.parameters_.getRawParameterValue(c.id)->load() - c.expected) < .02f);
    }
    auto& undo = f.p.session_.undo_manager;
    REQUIRE(undo.canUndo());
    undo.undo();
    CHECK(std::abs(f.p.parameters_.getRawParameterValue(params::kMusic)->load()) < .005f);
    CHECK(std::abs(f.p.parameters_.getRawParameterValue(params::kGuard)->load() - .25f) < .005f);
}

TEST_CASE("the rider fader rides by hand without an undo step, and edits the automation in Read mode", "[editor]") {
    Fixture f;
    auto& fader = f.editor->getRiderForTesting().getFader();
    CHECK(f.editor->getControlParameterIndex(fader) == f.p.getRide()->getParameterIndex());
    const auto handler = fader.createAccessibilityHandler();
    REQUIRE(handler != nullptr);
    CHECK(handler->getRole() == juce::AccessibilityRole::slider);
    f.p.session_.undo_manager.clearUndoHistory();

    // Off: a manual ride, over at once (the gesture ends with the edit), and nothing to undo
    handler->getValueInterface()->setValueAsString("-5");
    CHECK(!f.p.isManualRide());
    CHECK(!f.p.session_.undo_manager.canUndo());

    // Read: the fader is the automation
    f.p.setMode(dsp::Mode::kRead);
    handler->getValueInterface()->setValueAsString("-5");
    CHECK(std::abs(f.p.getRide()->get() + 5.f) < .02f);
    CHECK(!f.p.session_.undo_manager.canUndo());
}

TEST_CASE("a typed fader value is applied, and riding carries on from it", "[editor]") {
    Fixture f;
    auto& fader = f.editor->getRiderForTesting().getFader();
    // begin, change and end all happen before the next audio block
    fader.createAccessibilityHandler()->getValueInterface()->setValueAsString("-5");
    juce::AudioBuffer<float> io(2, 480);
    io.clear();
    juce::MidiBuffer midi;
    f.p.processBlock(io, midi);
    // the gain jumped there; in this long silence it has since begun its slow glide back to idle (one hop's worth)
    CHECK(std::abs(f.p.getEngine().getGainDb() + 5.f) < .2f);
    CHECK(!f.p.isManualRide());
}

TEST_CASE("in Read mode the fader covers the whole Rider range", "[editor]") {
    Fixture f;
    f.p.setMode(dsp::Mode::kRead);
    *f.p.getRide() = 18.f;
    juce::AudioBuffer<float> io(2, 480);
    io.clear();
    juce::MidiBuffer midi;
    f.p.processBlock(io, midi);
    f.editor->runFrameForTesting(1.1);
    auto& fader = f.editor->getRiderForTesting().getFader();
    CHECK(fader.getRange().end >= params::kRideLimit);
    CHECK(std::abs(fader.getValue() - 18.f) < .01f);
    // an edit near the top is not clamped to the riding range
    fader.createAccessibilityHandler()->getValueInterface()->setValueAsString("20");
    CHECK(std::abs(f.p.getRide()->get() - 20.f) < .02f);
}

TEST_CASE("closing the editor in the middle of a ride hands the fader back", "[editor]") {
    for (const auto mode : {dsp::Mode::kOff, dsp::Mode::kRead}) {
        CAPTURE(static_cast<int>(mode));
        Fixture f;
        f.p.setMode(mode);
        struct Gestures final : juce::AudioProcessorParameter::Listener {
            int open{0};
            void parameterValueChanged(int, float) override {}
            void parameterGestureChanged(int, const bool starting) override { open += starting ? 1 : -1; }
        } gestures;
        f.p.getRide()->addListener(&gestures);
        // what a scroll on the fader starts; it would end after a pause, but the editor goes first
        auto& fader = f.editor->getRiderForTesting().getFader();
        fader.on_begin();
        fader.on_change(-3.f);
        CHECK((f.p.isManualRide() || gestures.open == 1));
        f.base.reset();
        CHECK(!f.p.isManualRide());
        CHECK(gestures.open == 0);
        f.p.getRide()->removeListener(&gestures);
    }
}

TEST_CASE("dragging the floor line sets the sensitivity in one undo step, and leaves the target", "[editor]") {
    Fixture f;
    auto& history = f.editor->getHistoryForTesting();
    REQUIRE(history.getWidth() > 0);
    f.p.session_.undo_manager.clearUndoHistory();
    const auto mouse = [&history](const juce::Point<float> at, const juce::Point<float> down, const bool dragged) {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {},
                                juce::MouseInputSource::defaultPressure, 0.f, 0.f, 0.f, 0.f, &history, &history, now,
                                down, now, 1, dragged);
    };
    const auto x = static_cast<float>(history.getWidth()) * .5f;
    const auto y = history.getFloorYForTesting();
    const juce::Point down{x, y};
    history.mouseMove(mouse(down, down, false));
    // hovering the line hands the host the sensitivity
    CHECK(f.editor->getControlParameterIndex(history) ==
          f.p.parameters_.getParameter(params::kSensitivity)->getParameterIndex());
    // down by 40 pixels: a lower floor, so more sensitivity
    history.mouseDown(mouse(down, down, false));
    history.mouseDrag(mouse({x, y + 20.f}, down, true));
    history.mouseDrag(mouse({x, y + 40.f}, down, true));
    history.mouseUp(mouse({x, y + 40.f}, down, true));
    const auto sensitivity = f.p.parameters_.getRawParameterValue(params::kSensitivity)->load();
    CHECK(sensitivity > 1.f);
    // the line ends up under the mouse, and the target has not moved
    CHECK(std::abs(history.getFloorYForTesting() - (y + 40.f)) < 1.f);
    CHECK(std::abs(f.p.parameters_.getRawParameterValue(params::kTarget)->load() - params::kTargetDefault) < .01f);
    REQUIRE(f.p.session_.undo_manager.canUndo());
    f.p.session_.undo_manager.undo();
    CHECK(std::abs(f.p.parameters_.getRawParameterValue(params::kSensitivity)->load()) < .01f);
    CHECK(!f.p.session_.undo_manager.canUndo());
}

TEST_CASE("the mode chips switch the automation mode", "[editor]") {
    Fixture f;
    auto& rider = f.editor->getRiderForTesting();
    for (const auto mode : {dsp::Mode::kWrite, dsp::Mode::kRead, dsp::Mode::kOff}) {
        auto& chip = rider.getModeChip(mode);
        chip.createAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press);
        CHECK(f.p.getMode() == mode);
        f.editor->runFrameForTesting(1.1);
        CHECK(chip.isOn());
    }
}

TEST_CASE("the history fills while the engine publishes, and learn sets the target as one undo step", "[editor]") {
    Fixture f;
    f.p.getEngine().setPublishing(true);
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(2, samples(rate, 2.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -31.f, true);
    auto& deck = f.editor->getDeckForTesting();
    deck.getLearnButton().createAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press);
    CHECK(f.p.getEngine().isLearning());
    juce::AudioBuffer<float> io(2, 480);
    juce::MidiBuffer midi;
    for (int pos = 0; pos + 480 <= input.getNumSamples(); pos += 480) {
        for (int c = 0; c < 2; ++c) {
            io.copyFrom(c, 0, input, c, pos, 480);
        }
        f.p.processBlock(io, midi);
    }
    f.editor->runFrameForTesting(1.2);
    CHECK(f.editor->getHistoryForTesting().getColumnCountForTesting() > 300);

    f.p.session_.undo_manager.clearUndoHistory();
    deck.getLearnButton().createAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press);
    CHECK(!f.p.getEngine().isLearning());
    CHECK(std::abs(f.p.parameters_.getRawParameterValue(params::kTarget)->load() + 31.f) < 1.f);
    CHECK(f.p.session_.undo_manager.canUndo());
}

TEST_CASE("factory presets load and set every parameter in the tree", "[presets]") {
    PluginProcessor p;
    const auto codec = altvane::ui::PresetCodec::xmlAsJson();
    const auto dir = juce::File::createTempFile("presets");
    dir.createDirectory();
    const auto presets = altvane::leveler::factoryPresets();
    CHECK(presets.size() == 8);
    for (const auto& [path, text] : presets) {
        CAPTURE(path);
        const auto file = dir.getChildFile(juce::File::createLegalFileName(path) + ".json");
        REQUIRE(file.replaceWithText(text));
        juce::MemoryBlock state;
        REQUIRE(codec.read(file, state).wasOk());
        p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
        REQUIRE(xml != nullptr);
        CHECK(xml->getNumChildElements() == params::allIDs().size());
    }
    dir.deleteRecursively();
    // the last one, Tight, is fast with a wide range
    CHECK(std::abs(p.parameters_.getRawParameterValue(params::kSpeed)->load() - .85f) < .005f);
    CHECK(std::abs(p.parameters_.getRawParameterValue(params::kRangeMax)->load() - 12.f) < .005f);
}
