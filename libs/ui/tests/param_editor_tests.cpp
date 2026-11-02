// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <altvane_ui/util/value_format.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "fake_processor.hpp"

using namespace altvane::ui;
using namespace altvane::ui::test;
using Catch::Matchers::WithinAbs;

namespace {
    juce::UndoManager& history(FakeProcessor& p) { return p.session_.undo_manager; }
}

TEST_CASE("setNow changes parameters as one undoable step with one gesture each", "[param_editor]") {
    FakeProcessor p;
    GestureCounter gain(p.para(ids::kGain)), freq(p.para(ids::kFreq));

    p.editor_.setNow({{p.para(ids::kGain), 6.f}, {p.para(ids::kFreq), 250.f}}, "Set both");
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(6.0, 1e-4));
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(250.0, 1e-2));
    CHECK(gain.begins == 1);
    CHECK(gain.ends == 1);
    CHECK(freq.begins == 1);
    CHECK(freq.ends == 1);
    CHECK_FALSE(p.editor_.isEditing());

    REQUIRE(history(p).canUndo());
    CHECK(history(p).getUndoDescription() == "Set both");

    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(1000.0, 1e-2));
    CHECK_FALSE(history(p).canUndo());
    // the host hears undo as gestures too
    CHECK(gain.begins == gain.ends);

    p.editor_.redo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(6.0, 1e-4));
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(250.0, 1e-2));
    CHECK_FALSE(history(p).canRedo());
}

TEST_CASE("an edit that changes nothing leaves no undo step", "[param_editor]") {
    FakeProcessor p;
    p.editor_.setNow({{p.para(ids::kGain), 0.f}}, "Nothing");
    CHECK_FALSE(history(p).canUndo());

    juce::RangedAudioParameter* paras[] = {p.para(ids::kGain)};
    p.editor_.beginEdit(paras, "Drag");
    p.editor_.setPlain(p.para(ids::kGain), 3.f);
    p.editor_.setPlain(p.para(ids::kGain), 0.f);
    p.editor_.endEdit();
    CHECK_FALSE(history(p).canUndo());
}

TEST_CASE("a continuous edit is one gesture and one undo step back to where it began", "[param_editor]") {
    FakeProcessor p;
    p.setPlain(ids::kGain, -3.f);
    GestureCounter gain(p.para(ids::kGain));

    juce::RangedAudioParameter* paras[] = {p.para(ids::kGain)};
    p.editor_.beginEdit(paras, "Change Gain");
    CHECK(p.editor_.isEditing());
    for (int i = 1; i <= 10; ++i) {
        p.editor_.setPlain(p.para(ids::kGain), -3.f + static_cast<float>(i));
    }
    CHECK(gain.begins == 1);
    CHECK(gain.ends == 0);
    p.editor_.endEdit();
    CHECK(gain.ends == 1);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(7.0, 1e-4));

    REQUIRE(history(p).canUndo());
    CHECK(history(p).getUndoDescription() == "Change Gain");
    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(-3.0, 1e-4));
    CHECK_FALSE(history(p).canUndo());
}

TEST_CASE("cancelling an edit restores the parameters and leaves no undo step", "[param_editor]") {
    FakeProcessor p;
    GestureCounter gain(p.para(ids::kGain));
    juce::RangedAudioParameter* paras[] = {p.para(ids::kGain), p.para(ids::kFreq)};
    p.editor_.beginEdit(paras, "Drag");
    p.editor_.setPlain(p.para(ids::kGain), 12.f);
    p.editor_.setPlain(p.para(ids::kFreq), 5000.f);
    p.editor_.cancelEdit();

    CHECK_FALSE(p.editor_.isEditing());
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(1000.0, 1e-2));
    CHECK_FALSE(history(p).canUndo());
    CHECK(gain.begins == 1);
    CHECK(gain.ends == 1);
}

TEST_CASE("plain values are clamped to the range and snapped to legal values", "[param_editor]") {
    FakeProcessor p;
    p.editor_.setNow({{p.para(ids::kGain), 99.f}}, "Too loud");
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(24.0, 1e-4));
    p.editor_.setNow({{p.para(ids::kGain), -99.f}}, "Too quiet");
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(-24.0, 1e-4));
    p.editor_.setNow({{p.para(ids::kGain), 1.234567f}}, "Fine");
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(1.23, 1e-4));

    p.editor_.setNow({{p.para(ids::kMode), 1.4f}}, "Mode");
    CHECK(p.plain(ids::kMode) == 1.f);
    p.editor_.setNow({{p.para(ids::kMode), 7.f}}, "Mode");
    CHECK(p.plain(ids::kMode) == 2.f);

    // a null parameter is ignored rather than dereferenced
    p.editor_.setPlain(nullptr, 1.f);
    p.editor_.toggle(nullptr, "Nothing");
}

TEST_CASE("toggle flips a switch as an undoable step", "[param_editor]") {
    FakeProcessor p;
    p.editor_.toggle(p.para(ids::kBypass), "Bypass");
    CHECK(p.plain(ids::kBypass) == 1.f);
    p.editor_.toggle(p.para(ids::kBypass), "Bypass");
    CHECK(p.plain(ids::kBypass) == 0.f);
    p.editor_.undo();
    CHECK(p.plain(ids::kBypass) == 1.f);
    p.editor_.undo();
    CHECK(p.plain(ids::kBypass) == 0.f);
    CHECK_FALSE(history(p).canUndo());
}

TEST_CASE("each edit is its own undo step, undone newest first", "[param_editor]") {
    FakeProcessor p;
    p.editor_.setNow({{p.para(ids::kGain), 1.f}}, "One");
    p.editor_.setNow({{p.para(ids::kGain), 2.f}}, "Two");
    p.editor_.setNow({{p.para(ids::kFreq), 100.f}}, "Three");

    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(1000.0, 1e-2));
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(2.0, 1e-4));
    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(1.0, 1e-4));
    p.editor_.redo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(2.0, 1e-4));

    // a new edit after undo drops the redo branch
    p.editor_.undo();
    p.editor_.setNow({{p.para(ids::kGain), 5.f}}, "Five");
    CHECK_FALSE(history(p).canRedo());
    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(1.0, 1e-4));
}

TEST_CASE("undo during an edit ends the edit and undoes it", "[param_editor]") {
    FakeProcessor p;
    GestureCounter gain(p.para(ids::kGain));
    juce::RangedAudioParameter* paras[] = {p.para(ids::kGain)};
    p.editor_.beginEdit(paras, "Drag");
    p.editor_.setPlain(p.para(ids::kGain), 9.f);
    p.editor_.undo();

    CHECK_FALSE(p.editor_.isEditing());
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
    CHECK(gain.begins == gain.ends);
    p.editor_.redo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(9.0, 1e-4));
}

TEST_CASE("a new edit ends the open one first", "[param_editor]") {
    FakeProcessor p;
    GestureCounter gain(p.para(ids::kGain));
    juce::RangedAudioParameter* first[] = {p.para(ids::kGain)};
    p.editor_.beginEdit(first, "First");
    const auto first_id = p.editor_.getEditId();
    p.editor_.setPlain(p.para(ids::kGain), 4.f);

    juce::RangedAudioParameter* second[] = {p.para(ids::kFreq)};
    p.editor_.beginEdit(second, "Second");
    CHECK(gain.ends == 1);
    CHECK_FALSE(p.editor_.isEditing(first_id));
    CHECK(p.editor_.isEditing(p.editor_.getEditId()));
    p.editor_.setPlain(p.para(ids::kFreq), 300.f);
    p.editor_.endEdit();

    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(1000.0, 1e-2));
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(4.0, 1e-4));
    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
}

TEST_CASE("an editor closed mid-edit ends the gesture and keeps the step", "[param_editor]") {
    FakeProcessor p;
    GestureCounter gain(p.para(ids::kGain));
    {
        ParamEditor editor(p.session_.undo_manager);
        juce::RangedAudioParameter* paras[] = {p.para(ids::kGain)};
        editor.beginEdit(paras, "Drag");
        editor.setPlain(p.para(ids::kGain), -6.f);
    }
    CHECK(gain.begins == 1);
    CHECK(gain.ends == 1);
    REQUIRE(history(p).canUndo());
    history(p).undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
}

TEST_CASE("setNotifyingHost changes a parameter outside the history", "[param_editor]") {
    FakeProcessor p;
    GestureCounter meter(p.para(ids::kMeter));
    ParamEditor::setNotifyingHost(p.para(ids::kMeter), .25f);
    CHECK_THAT(p.plain(ids::kMeter), WithinAbs(.25, 1e-6));
    CHECK(meter.begins == 1);
    CHECK(meter.ends == 1);
    CHECK_FALSE(history(p).canUndo());
}

// ---------------------------------------------------------------- bindValue: a control wired to a parameter

namespace {
    struct BoundKnob {
        FakeProcessor p;
        Knob knob;
        GestureCounter gestures{p.para(ids::kGain)};

        BoundKnob() {
            bindValue(knob, p.editor_, p.para(ids::kGain), "Gain");
            knob.setSize(80, 100);
        }

        juce::TextEditor* textEditor() const {
            for (auto* child : knob.getChildren()) {
                if (auto* editor = dynamic_cast<juce::TextEditor*>(child)) {
                    return editor;
                }
            }
            return nullptr;
        }

        /**
         * open the control's text entry, type and press a key, as a user would
         */
        void type(const juce::String& text, const int key = juce::KeyPress::returnKey) {
            knob.beginTextEdit();
            auto* editor = textEditor();
            REQUIRE(editor != nullptr);
            editor->setText(text, false);
            editor->keyPressed(juce::KeyPress(key));
            // the text editor tells its listeners about return and escape asynchronously
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            REQUIRE(textEditor() == nullptr);
        }
    };
}

TEST_CASE("bindValue gives the control the parameter's range, default and ID", "[param_editor][typing]") {
    BoundKnob b;
    CHECK(b.knob.getComponentID() == ids::kGain);
    CHECK(b.knob.getRange().start == -24.f);
    CHECK(b.knob.getRange().end == 24.f);
    CHECK(b.knob.getTitle() == "Gain");
    REQUIRE(b.knob.parse);
}

TEST_CASE("typing a value sets the parameter as one gesture and one undo step", "[param_editor][typing]") {
    BoundKnob b;
    b.type(format::kMinus + "6.5 dB");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(-6.5, 1e-4));
    CHECK_THAT(b.knob.getValue(), WithinAbs(-6.5, 1e-4));
    CHECK(b.gestures.begins == 1);
    CHECK(b.gestures.ends == 1);
    REQUIRE(b.p.session_.undo_manager.canUndo());
    CHECK(b.p.session_.undo_manager.getUndoDescription() == "Change Gain");

    b.p.editor_.undo();
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
}

TEST_CASE("typed values accept hyphens, signs and no unit", "[param_editor][typing]") {
    BoundKnob b;
    b.type("-3");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(-3.0, 1e-4));
    b.type("+4.25");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(4.25, 1e-4));
    b.type("2,5 dB");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(2.5, 1e-4));
}

TEST_CASE("a typed value outside the range is clamped", "[param_editor][typing]") {
    BoundKnob b;
    b.type("+99");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(24.0, 1e-4));
    b.type("-1000 dB");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(-24.0, 1e-4));
}

TEST_CASE("typing junk changes nothing and leaves no undo step", "[param_editor][typing]") {
    BoundKnob b;
    b.p.setPlain(ids::kGain, 3.f);
    for (const auto* junk : {"", "loud", "dB", "-inf"}) {
        INFO("text: '" << junk << "'");
        b.type(junk);
        CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(3.0, 1e-4));
    }
    CHECK(b.gestures.begins == 0);
    CHECK_FALSE(b.p.session_.undo_manager.canUndo());
}

TEST_CASE("typing junk before a number changes nothing", "[param_editor][typing]") {
    // it used to set the parameter to 0, as an undoable edit
    BoundKnob b;
    b.p.setPlain(ids::kGain, 3.f);
    b.type("x5");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(3.0, 1e-4));
    CHECK(b.gestures.begins == 0);
    CHECK_FALSE(b.p.session_.undo_manager.canUndo());
}

TEST_CASE("escape closes the text entry without a change", "[param_editor][typing]") {
    BoundKnob b;
    b.type("12", juce::KeyPress::escapeKey);
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
    CHECK(b.gestures.begins == 0);
}

TEST_CASE("accessibility clients type values through the same parser", "[param_editor][typing]") {
    BoundKnob b;
    const auto handler = b.knob.createAccessibilityHandler();
    auto* value = handler->getValueInterface();
    REQUIRE(value != nullptr);
    value->setValueAsString("-12 dB");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(-12.0, 1e-4));
    value->setValueAsString("nonsense");
    CHECK_THAT(b.p.plain(ids::kGain), WithinAbs(-12.0, 1e-4));
    CHECK(b.gestures.begins == 1);
    CHECK(b.gestures.ends == 1);
}

TEST_CASE("a control ends only its own edit", "[param_editor][typing]") {
    // a wheel's edit ends on a timer, after another control may have begun its own
    FakeProcessor p;
    Knob a, b;
    bindValue(a, p.editor_, p.para(ids::kGain), "Gain");
    bindValue(b, p.editor_, p.para(ids::kFreq), "Freq");
    GestureCounter gain(p.para(ids::kGain)), freq(p.para(ids::kFreq));

    a.on_begin();
    a.on_change(5.f);
    b.on_begin();
    b.on_change(200.f);
    // a's wheel timer fires: b's edit must stay open
    a.on_end();
    CHECK(p.editor_.isEditing());
    CHECK(freq.ends == 0);
    b.on_end();
    CHECK_FALSE(p.editor_.isEditing());

    // a's next wheel notch after b took over opens a fresh edit rather than change gain outside a gesture
    a.on_change(6.f);
    CHECK(p.editor_.isEditing());
    a.on_end();
    CHECK(gain.begins == gain.ends);
    CHECK(freq.begins == freq.ends);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(6.0, 1e-4));

    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(5.0, 1e-4));
    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(1000.0, 1e-2));
    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
}
