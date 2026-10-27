// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include <altvane_ui/session/preset_manager.hpp>

#include "PluginProcessor.hpp"
#include "ui/editor.hpp"
#include "ui/factory_presets.hpp"
#include "signals.hpp"

using namespace altvane::deesser;

namespace {
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 480;

    struct Fixture {
        PluginProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor{nullptr};
        double time{1.0};

        Fixture() {
            p.setPlayConfigDetails(2, 2, kRate, kBlock);
            p.prepareToPlay(kRate, kBlock);
            base.reset(p.createEditor());
            editor = dynamic_cast<Editor*>(base.get());
            editor->setSize(1000, 640);
            editor->runFrameForTesting(time);
        }

        ~Fixture() {
            base.reset();
            p.releaseResources();
        }

        float value(const char* id) const {
            return p.parameters_.getRawParameterValue(id)->load();
        }

        /**
         * play a buffer (in blocks), with the editor running a frame every 10 ms
         */
        void play(const juce::AudioBuffer<float>& input) {
            juce::AudioBuffer<float> io(2, kBlock);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + kBlock <= input.getNumSamples(); pos += kBlock) {
                for (int c = 0; c < 2; ++c) {
                    io.copyFrom(c, 0, input, std::min(c, input.getNumChannels() - 1), pos, kBlock);
                }
                p.processBlock(io, midi);
                time += kBlock / kRate;
                editor->runFrameForTesting(time);
            }
        }
    };

    juce::MouseEvent mouse(juce::Component& c, const juce::Point<float> p, const juce::Point<float> down,
                           const bool dragged, const juce::ModifierKeys mods = {}) {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return {source, p, mods, juce::MouseInputSource::defaultPressure, 0.f, 0.f, 0.f, 0.f, &c, &c, now, down, now,
                1, dragged};
    }

    /**
     * vowels and esses, the esses at known levels
     */
    juce::AudioBuffer<float> phrase(const std::vector<float>& ess_peaks) {
        const auto ms = [](const double m) { return static_cast<int>(m * .001 * kRate); };
        juce::AudioBuffer<float> b(1, ms(400.0 * static_cast<double>(ess_peaks.size()) + 400.0));
        b.clear();
        for (size_t i = 0; i < ess_peaks.size(); ++i) {
            const auto at = static_cast<double>(i) * 400.0;
            signals::addVowel(b, 0, ms(at), ms(200), kRate, .4f);
            signals::addEss(b, 0, ms(at + 240), ms(120), kRate, ess_peaks[i], 7000.0, static_cast<int>(i) + 1);
        }
        return b;
    }
}

TEST_CASE("threshold and range are sliders that edit with one undo step", "[editor]") {
    Fixture f;
    auto& deck = f.editor->getDeckForTesting();
    for (auto* knob : {&deck.getThreshold(), &deck.getRange()}) {
        const auto id = knob == &deck.getThreshold() ? params::kThreshold : params::kRange;
        CAPTURE(id);
        // the host's parameter menu finds the parameter under the knob
        CHECK(f.editor->getControlParameterIndex(*knob) == f.p.parameters_.getParameter(id)->getParameterIndex());
        const auto handler = knob->createAccessibilityHandler();
        REQUIRE(handler != nullptr);
        CHECK(handler->getRole() == juce::AccessibilityRole::slider);
        auto* value = handler->getValueInterface();
        REQUIRE(value != nullptr);
        value->setValueAsString("-12");
        CHECK(std::abs(f.value(id) - (id == params::kThreshold ? -12.f : 0.f)) < .05f);
    }
    f.p.session_.undo_manager.undo();
    CHECK(std::abs(f.value(params::kRange) - params::kRangeDefault) < .05f);
}

TEST_CASE("dragging the threshold in the history is one undo step", "[editor]") {
    Fixture f;
    auto& history = f.editor->getHistoryForTesting();
    const auto x = static_cast<float>(history.getWidth()) * .5f;
    const auto y = history.getThresholdY();
    const juce::Point down{x, y};
    history.mouseMove(mouse(history, down, down, false));
    // hovering the line hands the host the threshold
    CHECK(f.editor->getControlParameterIndex(history) == f.p.parameters_.getParameter(params::kThreshold)->getParameterIndex());
    history.mouseDown(mouse(history, down, down, false));
    history.mouseDrag(mouse(history, {x, y - 40.f}, down, true));
    history.mouseDrag(mouse(history, {x, y - 60.f}, down, true));
    history.mouseUp(mouse(history, {x, y - 60.f}, down, true));
    CHECK(f.value(params::kThreshold) > params::kThresholdDefault + 3.f);
    // the click did not freeze the history
    CHECK_FALSE(history.isFrozen());
    f.p.session_.undo_manager.undo();
    CHECK(std::abs(f.value(params::kThreshold) - params::kThresholdDefault) < .05f);

    // a click elsewhere freezes it
    const juce::Point empty{x, history.getThresholdY() + 80.f};
    history.mouseDown(mouse(history, empty, empty, false));
    history.mouseUp(mouse(history, empty, empty, false));
    CHECK(history.isFrozen());
}

TEST_CASE("moving the band moves both edges in one undo step", "[editor]") {
    Fixture f;
    auto* low = f.p.parameters_.getParameter(params::kBandLow);
    auto* high = f.p.parameters_.getParameter(params::kBandHigh);
    low->setValueNotifyingHost(low->convertTo0to1(4000.f));
    high->setValueNotifyingHost(high->convertTo0to1(8000.f));
    f.editor->runFrameForTesting(f.time += .02);
    f.p.session_.undo_manager.clearUndoHistory();

    auto& band = f.editor->getBandViewForTesting();
    const auto y = band.getPlot().getCentreY();
    const juce::Point down{band.freqX(5600.f), y};
    band.mouseDown(mouse(band, down, down, false));
    band.mouseDrag(mouse(band, {band.freqX(5600.f * 1.25f), y}, down, true));
    band.mouseUp(mouse(band, {band.freqX(5600.f * 1.25f), y}, down, true));
    CHECK(std::abs(f.value(params::kBandLow) / 5000.f - 1.f) < .03f);
    CHECK(std::abs(f.value(params::kBandHigh) / 10000.f - 1.f) < .03f);
    f.p.session_.undo_manager.undo();
    CHECK(std::abs(f.value(params::kBandLow) - 4000.f) < 1.f);
    CHECK(std::abs(f.value(params::kBandHigh) - 8000.f) < 1.f);
    f.editor->runFrameForTesting(f.time += .02);

    // the host menu follows the hovered edge
    const juce::Point edge{band.freqX(4000.f), y};
    band.mouseMove(mouse(band, edge, edge, false));
    CHECK(f.editor->getControlParameterIndex(band) == low->getParameterIndex());
    // dragging the high edge to the far right opens the band
    const juce::Point high_edge{band.freqX(8000.f), y};
    band.mouseDown(mouse(band, high_edge, high_edge, false));
    band.mouseDrag(mouse(band, {band.getPlot().getRight(), y}, high_edge, true));
    band.mouseUp(mouse(band, {band.getPlot().getRight(), y}, high_edge, true));
    CHECK(params::isOpen(f.value(params::kBandHigh)));
}

TEST_CASE("the finder fits the band to the esses in one undo step", "[editor]") {
    Fixture f;
    auto& band = f.editor->getBandViewForTesting();
    CHECK_FALSE(band.fitToFinder());
    f.play(phrase({.25f, .3f, .2f, .3f, .25f, .3f, .2f, .25f}));
    REQUIRE(f.editor->getAnalyzerForTesting().getFinder().confidence > 0.f);
    const auto found = f.editor->getAnalyzerForTesting().getFinder().hz;
    f.p.session_.undo_manager.clearUndoHistory();
    REQUIRE(band.fitToFinder());
    // a shelf starting just under the esses
    CHECK(std::abs(f.value(params::kBandLow) - found / 1.3f) < 2.f);
    f.p.session_.undo_manager.undo();
    CHECK(std::abs(f.value(params::kBandLow) - params::kBandLowDefault) < 1.f);
    CHECK_FALSE(f.p.session_.undo_manager.canUndo());
}

TEST_CASE("Learn sets the threshold from the esses heard, as one undo step", "[editor]") {
    Fixture f;
    auto& deck = f.editor->getDeckForTesting();
    f.p.session_.undo_manager.clearUndoHistory();
    deck.toggleLearn();
    CHECK(deck.isLearning());
    f.play(phrase({.3f, .2f, .25f, .3f, .2f}));
    deck.toggleLearn();
    CHECK_FALSE(deck.isLearning());
    const auto threshold = f.value(params::kThreshold);
    CHECK(std::abs(threshold - params::kThresholdDefault) > 1.f);
    CHECK(threshold > params::kThresholdMin);
    CHECK(threshold < params::kThresholdMax);
    f.p.session_.undo_manager.undo();
    CHECK(std::abs(f.value(params::kThreshold) - params::kThresholdDefault) < .05f);

    // with too little to go on it leaves the threshold alone
    deck.toggleLearn();
    deck.toggleLearn();
    CHECK(std::abs(f.value(params::kThreshold) - params::kThresholdDefault) < .05f);
}

TEST_CASE("listening and learning are neither edits nor saved", "[editor]") {
    Fixture f;
    f.p.session_.undo_manager.clearUndoHistory();
    f.p.setListen(dsp::Listen::kTrigger);
    f.editor->getDeckForTesting().toggleLearn();
    f.editor->runFrameForTesting(f.time += .02);
    CHECK_FALSE(f.p.session_.undo_manager.canUndo());
    juce::MemoryBlock state;
    f.p.getStateInformation(state);
    PluginProcessor other;
    other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(other.getListen() == dsp::Listen::kOff);
}

TEST_CASE("factory presets set every parameter", "[editor]") {
    const auto presets = factoryPresets();
    CHECK(presets.size() >= 8);
    for (const auto& [name, text] : presets) {
        CAPTURE(name);
        const auto json = juce::JSON::parse(text);
        REQUIRE(json.isObject());
        juce::StringArray ids;
        for (const auto& child : *json.getProperty("children", {}).getArray()) {
            ids.add(child.getProperty("id", {}).toString());
        }
        for (const auto& id : params::allIDs()) {
            CAPTURE(id);
            CHECK(ids.contains(id));
        }
        // and loading one gives a valid state
        const auto file = juce::File::createTempFile(".json");
        file.replaceWithText(text);
        juce::MemoryBlock block;
        REQUIRE(altvane::ui::PresetCodec::xmlAsJson().read(file, block).wasOk());
        PluginProcessor p;
        p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        CHECK(p.parameters_.getRawParameterValue(params::kThreshold)->load() < 0.f);
        file.deleteFile();
    }
}

TEST_CASE("the band's edges can be typed and read as values", "[editor]") {
    Fixture f;
    auto& band = f.editor->getBandViewForTesting();
    for (auto* field : {&band.getLowField(), &band.getHighField()}) {
        const auto handler = field->createAccessibilityHandler();
        REQUIRE(handler != nullptr);
        CHECK(handler->getRole() == juce::AccessibilityRole::slider);
    }
    band.getLowField().createAccessibilityHandler()->getValueInterface()->setValueAsString("4.2 kHz");
    CHECK(std::abs(f.value(params::kBandLow) - 4200.f) < 1.f);
    band.getHighField().createAccessibilityHandler()->getValueInterface()->setValueAsString("9k");
    CHECK(std::abs(f.value(params::kBandHigh) - 9000.f) < 1.f);
    band.getHighField().createAccessibilityHandler()->getValueInterface()->setValueAsString("open");
    CHECK(params::isOpen(f.value(params::kBandHigh)));
    CHECK(f.editor->getControlParameterIndex(band.getLowField())
          == f.p.parameters_.getParameter(params::kBandLow)->getParameterIndex());
}

TEST_CASE("a control only ends its own edit", "[editor]") {
    Fixture f;
    auto& deck = f.editor->getDeckForTesting();
    auto& threshold = deck.getThreshold();
    auto& range = deck.getRange();
    // a wheel on the threshold, then a drag on the range before the wheel's timer ends it
    threshold.on_begin();
    threshold.on_change(-30.f);
    range.on_begin();
    range.on_change(12.f);
    // the threshold's timer fires: the range's drag stays open
    threshold.on_end();
    range.on_change(15.f);
    range.on_end();
    CHECK(std::abs(f.value(params::kRange) - 15.f) < .05f);
    auto& undo = f.p.session_.undo_manager;
    undo.undo();
    CHECK(std::abs(f.value(params::kRange) - params::kRangeDefault) < .05f);
    CHECK(std::abs(f.value(params::kThreshold) + 30.f) < .05f);
}

TEST_CASE("Learn waits for a drag to finish before setting the threshold", "[editor]") {
    Fixture f;
    auto& deck = f.editor->getDeckForTesting();
    deck.toggleLearn();
    auto& range = deck.getRange();
    range.on_begin();
    range.on_change(14.f);
    std::vector<float> peaks(18, .3f);
    for (size_t i = 0; i < peaks.size(); ++i) {
        peaks[i] = i % 2 == 0 ? .3f : .2f;
    }
    f.play(phrase(peaks));
    // enough esses, but the range is still being dragged
    CHECK(std::abs(f.value(params::kThreshold) - params::kThresholdDefault) < .05f);
    range.on_change(16.f);
    range.on_end();
    f.editor->runFrameForTesting(f.time += .02);
    CHECK_FALSE(deck.isLearning());
    CHECK(std::abs(f.value(params::kThreshold) - params::kThresholdDefault) > 1.f);
    // two steps: the drag, then the learned threshold
    auto& undo = f.p.session_.undo_manager;
    undo.undo();
    CHECK(std::abs(f.value(params::kThreshold) - params::kThresholdDefault) < .05f);
    CHECK(std::abs(f.value(params::kRange) - 16.f) < .05f);
}

TEST_CASE("text without a number leaves a value alone", "[editor]") {
    Fixture f;
    auto& deck = f.editor->getDeckForTesting();
    deck.getThreshold().createAccessibilityHandler()->getValueInterface()->setValueAsString("loud");
    CHECK(std::abs(f.value(params::kThreshold) - params::kThresholdDefault) < .05f);
    auto& band = f.editor->getBandViewForTesting();
    band.getLowField().createAccessibilityHandler()->getValueInterface()->setValueAsString("x");
    CHECK(std::abs(f.value(params::kBandLow) - params::kBandLowDefault) < 1.f);
}

TEST_CASE("typing an edge past the other moves both, as one undo step", "[editor]") {
    Fixture f;
    auto& band = f.editor->getBandViewForTesting();
    f.p.session_.undo_manager.clearUndoHistory();
    band.getHighField().createAccessibilityHandler()->getValueInterface()->setValueAsString("2 kHz");
    CHECK(std::abs(f.value(params::kBandHigh) - 2000.f) < 1.f);
    CHECK(f.value(params::kBandLow) * params::kMinBandRatio <= f.value(params::kBandHigh) + 1.f);
    f.p.session_.undo_manager.undo();
    CHECK(std::abs(f.value(params::kBandLow) - params::kBandLowDefault) < 1.f);
    CHECK(params::isOpen(f.value(params::kBandHigh)));
}
