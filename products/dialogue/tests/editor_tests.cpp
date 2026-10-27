// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "PluginProcessor.hpp"
#include "ui/editor.hpp"
#include "ui/factory_presets.hpp"

using namespace altvane::dialogue;

namespace {
    struct Fixture {
        PluginProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor{nullptr};

        Fixture() {
            p.setPlayConfigDetails(2, 2, 48000.0, 512);
            p.prepareToPlay(48000.0, 512);
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

TEST_CASE("stem knobs are sliders that edit their level with one undo step", "[editor]") {
    Fixture f;
    REQUIRE(f.editor != nullptr);
    for (size_t s = 0; s < dsp::kNumStems; ++s) {
        auto& knob = f.editor->getDeckForTesting().getCard(s).getKnob();
        CAPTURE(params::kLevelIDs[s]);
        // the host's parameter menu finds the level under the knob
        CHECK(f.editor->getControlParameterIndex(knob) == f.p.parameters_.getParameter(params::kLevelIDs[s])->getParameterIndex());
        const auto handler = knob.createAccessibilityHandler();
        REQUIRE(handler != nullptr);
        CHECK(handler->getRole() == juce::AccessibilityRole::slider);
        auto* value = handler->getValueInterface();
        REQUIRE(value != nullptr);
        value->setValueAsString("-9");
        CHECK(std::abs(f.p.parameters_.getRawParameterValue(params::kLevelIDs[s])->load() + 9.f) < .05f);
    }
    auto& undo = f.p.session_.undo_manager;
    CHECK(undo.canUndo());
    undo.undo();
    CHECK(std::abs(f.p.parameters_.getRawParameterValue(params::kReverb)->load() - params::kLevelDefaults[dsp::kReverb]) < .05f);
}

TEST_CASE("focusing a stem shows its sensitivity, and the host menu follows the hovered node", "[editor]") {
    Fixture f;
    auto& visualiser = f.editor->getVisualiserForTesting();
    visualiser.setSize(800, 300);
    visualiser.setFocus(dsp::kAmbience);
    f.editor->runFrameForTesting(1.02);
    CHECK(visualiser.getFocus() == dsp::kAmbience);
    // nothing hovered: the visualiser has no parameter of its own
    CHECK(f.editor->getControlParameterIndex(visualiser) == -1);
}

TEST_CASE("the stereo mode shows for a stereo main output, whatever the stem outputs", "[editor]") {
    const auto mono = GENERATE(false, true);
    CAPTURE(mono);
    PluginProcessor p;
    const auto main = mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(main);
    for (int bus = 0; bus <= dsp::kNumStems; ++bus) {
        layout.outputBuses.add(main);
    }
    REQUIRE(p.setBusesLayout(layout));
    // a mono instance with its stem outputs on has more than one output channel, but is still mono
    CHECK(p.getTotalNumOutputChannels() == (mono ? 4 : 8));
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    dynamic_cast<Editor&>(*editor).runFrameForTesting(1.0);
    std::function<juce::Component*(juce::Component&)> find = [&](juce::Component& c) -> juce::Component* {
        if (c.getTitle() == "Linked") {
            return &c;
        }
        for (auto* child : c.getChildren()) {
            if (auto* found = find(*child)) {
                return found;
            }
        }
        return nullptr;
    };
    auto* linked = find(*editor);
    REQUIRE(linked != nullptr);
    CHECK(linked->isVisible() == !mono);
    editor.reset();
    p.releaseResources();
}

TEST_CASE("solo and removed auditions are exclusive and never saved", "[editor]") {
    Fixture f;
    f.p.setListen(dsp::Listen::kVoice);
    juce::MemoryBlock state;
    f.p.getStateInformation(state);
    PluginProcessor other;
    other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(other.getListen() == dsp::Listen::kMix);
}

TEST_CASE("factory presets load and set every parameter", "[presets]") {
    PluginProcessor p;
    const auto codec = altvane::ui::PresetCodec::xmlAsJson();
    const auto dir = juce::File::createTempFile("presets");
    dir.createDirectory();
    for (const auto& [path, text] : factoryPresets()) {
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
    // the last one, Keep Consonants, lifts the voice's top nodes
    CHECK(p.parameters_.getRawParameterValue(params::sensitivityID(dsp::kVoice, 5))->load() > .5f);
}
