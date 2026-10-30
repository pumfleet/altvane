// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "test_helpers.hpp"
#include "ui/editor.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;

namespace {
    /**
     * an editor with the side-chain drawer open
     */
    struct Fixture {
        PluginProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        altvane::compressor::Editor* editor{nullptr};
        double t{1.0};

        Fixture() {
            prepare(p);
            set(p.na_parameters_, zlstate::PSideControlDisplay::kID, 1.f);
            base.reset(p.createEditor());
            editor = dynamic_cast<altvane::compressor::Editor*>(base.get());
            REQUIRE(editor != nullptr);
            for (int i = 0; i < 30; ++i) {
                frame();
            }
            REQUIRE(editor->getSideChainForTesting().isVisible());
        }

        ~Fixture() {
            if (base != nullptr) {
                p.editorBeingDeleted(base.get());
            }
            base.reset();
        }

        void frame() {
            t += 1.0 / 60.0;
            editor->runFrameForTesting(t);
        }

        altvane::compressor::SideEqView& eq() const {
            return editor->getSideChainForTesting().getEqForTesting();
        }

        bool press(const int key, const juce::ModifierKeys mods = {}) const {
            return eq().keyPressed(juce::KeyPress(key, mods, 0));
        }

        float band(const char* id, const size_t band) {
            return get(p, id + juce::String(band));
        }
    };
}

TEST_CASE("the side-chain EQ can be used from the keyboard", "[side-eq]") {
    Fixture f;
    auto& eq = f.eq();
    auto& undo = f.p.session_.undo_manager;
    CHECK(eq.getWantsKeyboardFocus());

    // Return adds a band and selects it
    CHECK(f.press(juce::KeyPress::returnKey));
    REQUIRE(eq.getSelected() == 0);
    CHECK(f.band(zlp::PFilterStatus::kID, 0) == 2.f);
    CHECK_THAT(f.band(zlp::PFreq::kID, 0), WithinAbs(1000.0, .5));
    CHECK(eq.describeSelection().startsWith("Band 1, Peak, 1"));

    // arrows move it, a semitone or half a dB a step, the burst one undo step
    CHECK(f.press(juce::KeyPress::rightKey));
    CHECK(f.press(juce::KeyPress::rightKey));
    CHECK(f.press(juce::KeyPress::upKey));
    CHECK_THAT(f.band(zlp::PFreq::kID, 0), WithinAbs(1000.0 * std::exp2(2.0 / 12.0), 1.0));
    CHECK_THAT(f.band(zlp::PGain::kID, 0), WithinAbs(.5, 1e-3));
    CHECK(f.press(juce::KeyPress::escapeKey));
    CHECK(eq.getSelected() == -1);
    undo.undo();
    CHECK_THAT(f.band(zlp::PFreq::kID, 0), WithinAbs(1000.0, .5));
    CHECK_THAT(f.band(zlp::PGain::kID, 0), WithinAbs(0.0, 1e-3));

    // a number selects a band, B bypasses it, Delete removes it
    f.frame();
    CHECK(f.press('1'));
    CHECK(eq.getSelected() == 0);
    CHECK(f.press('b'));
    CHECK(f.band(zlp::PFilterStatus::kID, 0) == 1.f);
    CHECK(eq.describeSelection().endsWith("bypassed"));
    CHECK(f.press(juce::KeyPress::deleteKey));
    CHECK(f.band(zlp::PFilterStatus::kID, 0) == 0.f);
    CHECK(eq.getSelected() == -1);
    CHECK(eq.describeSelection() == "No bands");
}

TEST_CASE("the selected band's values can be typed and read", "[side-eq]") {
    Fixture f;
    auto& eq = f.eq();
    REQUIRE(eq.addBand(500.f, 3.f) == 0);
    f.frame();
    auto& freq = eq.getFreqFieldForTesting();
    CHECK(freq.isVisible());
    CHECK(freq.getTitle() == "Band 1 frequency");
    CHECK(f.editor->getControlParameterIndex(freq)
          == f.p.parameters_.getParameter(zlp::PFreq::kID + juce::String(0))->getParameterIndex());

    // as a screen reader or a typed entry would
    const auto handler = freq.createAccessibilityHandler();
    REQUIRE(handler != nullptr);
    REQUIRE(handler->getValueInterface() != nullptr);
    CHECK(handler->getValueInterface()->getCurrentValueAsString() == "500 Hz");
    handler->getValueInterface()->setValueAsString("2.5k");
    CHECK_THAT(f.band(zlp::PFreq::kID, 0), WithinAbs(2500.0, 1.0));
    f.p.session_.undo_manager.undo();
    CHECK_THAT(f.band(zlp::PFreq::kID, 0), WithinAbs(500.0, 1.0));

    // the view itself reads out the selection
    const auto view = eq.createAccessibilityHandler();
    REQUIRE(view->getValueInterface() != nullptr);
    f.frame();
    CHECK(view->getValueInterface()->getCurrentValueAsString() == eq.describeSelection());
    CHECK(eq.describeSelection().contains("+3.0 dB"));
}

TEST_CASE("band solo plays the side chain in the band's range, and stops with the band or the editor", "[side-eq]") {
    Fixture f;
    auto& eq = f.eq();
    REQUIRE(eq.addBand(1000.f, 0.f) == 0);
    f.frame();
    auto& controller = f.p.getEqualizeController();

    // the Solo switch latches the selected band
    eq.getSoloChipForTesting().on_click();
    CHECK(eq.getSolo() == 0);
    CHECK(controller.getSoloBand() == 0);
    CHECK(eq.describeSelection().endsWith("soloed"));

    // a tone in the band's range plays at its level, one far below it hardly at all
    const auto in_band = run(f.p, {sine(1000.0, -12.f)}, seconds(.5));
    CHECK_THAT(peakDB(in_band[0], static_cast<size_t>(seconds(.25))), WithinAbs(-12.0, 1.0));
    const auto below = run(f.p, {sine(100.0, -12.f)}, seconds(.5));
    CHECK(peakDB(below[0], static_cast<size_t>(seconds(.25))) < -24.f);

    // the key holds a solo only while it is down; leaving the view lets it go too
    eq.getSoloChipForTesting().on_click();
    CHECK(controller.getSoloBand() == zlp::kBandNum);
    CHECK(f.eq().keyPressed(juce::KeyPress('s')));
    CHECK(controller.getSoloBand() == 0);
    eq.focusLost(juce::Component::focusChangedDirectly);
    CHECK(controller.getSoloBand() == zlp::kBandNum);

    // deleting the band ends its solo
    eq.getSoloChipForTesting().on_click();
    REQUIRE(controller.getSoloBand() == 0);
    f.p.parameters_.getParameter(zlp::PFilterStatus::kID + juce::String(0))->setValueNotifyingHost(0.f);
    f.frame();
    CHECK(controller.getSoloBand() == zlp::kBandNum);

    // and so does closing the drawer or the editor
    REQUIRE(eq.addBand(1000.f, 0.f) == 0);
    eq.getSoloChipForTesting().on_click();
    REQUIRE(controller.getSoloBand() == 0);
    set(f.p.na_parameters_, zlstate::PSideControlDisplay::kID, 0.f);
    for (int i = 0; i < 30; ++i) {
        f.frame();
    }
    CHECK(controller.getSoloBand() == zlp::kBandNum);
    set(f.p.na_parameters_, zlstate::PSideControlDisplay::kID, 1.f);
    for (int i = 0; i < 30; ++i) {
        f.frame();
    }
    f.eq().select(0);
    f.eq().getSoloChipForTesting().on_click();
    REQUIRE(controller.getSoloBand() == 0);
    f.p.editorBeingDeleted(f.base.get());
    f.base.reset();
    CHECK(controller.getSoloBand() == zlp::kBandNum);
}

TEST_CASE("Learn sets makeup so the output matches the input loudness, as one undo step", "[editor]") {
    Fixture f;
    set(f.p, zlp::PSideStereoMode::kID, 1.f);
    set(f.p, zlp::PSideStereoLink::kID, 100.f);
    set(f.p, zlp::PKneeW::kID, 0.f);
    set(f.p, zlp::PAttack::kID, 0.f);
    set(f.p, zlp::PRelease::kID, 2000.f);
    set(f.p, zlp::PThreshold::kID, -30.f);
    set(f.p, zlp::PRatio::kID, 4.f);
    auto& card = f.editor->getCardForTesting();

    card.toggleLearn();
    CHECK(card.isLearning());
    // 20 dB over the threshold at 4:1: the output is 15 dB quieter than the input
    run(f.p, {sine(1000.0, -10.f)}, seconds(3.0));
    card.toggleLearn();
    CHECK_FALSE(card.isLearning());
    CHECK_THAT(get(f.p, zlp::POutGain::kID), WithinAbs(15.0, 1.0));
    f.p.session_.undo_manager.undo();
    CHECK_THAT(get(f.p, zlp::POutGain::kID), WithinAbs(0.0, 1e-3));
}

TEST_CASE("controls explain themselves in help balloons, which the settings can turn off", "[editor]") {
    Fixture f;
    CHECK(f.editor->getUISettings().getSwitch(altvane::compressor::Editor::kHelpSwitch)
          == f.editor->getHelpForTesting().isActive());
    // every control that edits a parameter carries help, read by assistive technology too (the displays, which
    // would put a balloon over the whole window, describe themselves instead)
    std::vector<juce::Component*> all{f.editor};
    for (size_t i = 0; i < all.size(); ++i) {
        for (auto* child : all[i]->getChildren()) {
            all.push_back(child);
        }
    }
    auto checked = 0;
    for (auto* c : all) {
        if (f.p.parameters_.getParameter(c->getComponentID()) != nullptr && c->isVisible()) {
            CAPTURE(c->getComponentID());
            CHECK((c->getHelpText().isNotEmpty() || c->getDescription().isNotEmpty()));
            ++checked;
        }
    }
    CHECK(checked > 20);
}
