// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <functional>
#include <optional>
#include <juce_audio_processors/juce_audio_processors.h>

#include "icons.hpp"
#include "../util/spring.hpp"

namespace altvane::ui {
    /**
     * shared behaviour for anything that edits one continuous value:
     * vertical drag (shift = fine), wheel, double-click / cmd-click to reset, inline text entry
     */
    class ValueControl : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer,
                         private juce::TextEditor::Listener {
    public:
        std::function<void()> on_begin;
        std::function<void(float)> on_change;
        std::function<void()> on_end;
        std::function<juce::String(float)> format = [](const float v) { return juce::String(v, 2); };
        std::function<std::optional<float>(const juce::String&)> parse;

        ValueControl();

        ~ValueControl() override;

        void setRange(const juce::NormalisableRange<float>& range, float default_plain);

        void setValue(float plain);

        float getValue() const { return value_; }

        float getNormalised() const { return range_.convertTo0to1(value_); }

        const juce::NormalisableRange<float>& getRange() const { return range_; }

        void setAccent(juce::Colour c);

        void setLabel(const juce::String& label);

        void setEnabledLook(bool enabled);

        void beginTextEdit();

        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        /**
         * set a value as if the user did it (keyboard, accessibility clients)
         */
        void setFromUserGesture(float plain);

    protected:
        juce::NormalisableRange<float> range_{0.f, 1.f};
        float value_{0.f}, default_{0.f};
        juce::Colour accent_;
        juce::String label_;
        bool hovered_{false}, dragging_{false}, enabled_look_{true}, focus_ring_{false};
        float drag_pixels_{220.f};
        // horizontal controls follow the mouse sideways as well as up and down
        bool horizontal_drag_{false};

        /**
         * area where a double-click opens the text editor
         */
        virtual juce::Rectangle<int> getTextArea() const = 0;

        void mouseEnter(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection) override;
        void focusLost(FocusChangeType) override;

        void paintFocusRing(juce::Graphics& g, juce::Rectangle<float> area, float radius) const;

    private:
        float drag_start_norm_{0.f};
        float last_drag_y_{0.f}, last_drag_x_{0.f};
        float drag_norm_{0.f};
        bool wheel_active_{false};
        std::unique_ptr<juce::TextEditor> editor_;

        void setFromUser(float plain);
        void resetToDefault();
        void nudge(float normalised_delta);
        bool showHostMenu(const juce::MouseEvent& e);
        void timerCallback() override;
        void textEditorReturnKeyPressed(juce::TextEditor&) override;
        void textEditorEscapeKeyPressed(juce::TextEditor&) override;
        void textEditorFocusLost(juce::TextEditor&) override;
        void closeEditor(bool commit);
    };

    /**
     * the primary rotary control: caption on top, arc in the middle, tabular value below
     */
    class Knob final : public ValueControl {
    public:
        /**
         * compact knobs live in panels; a hero knob is one of a product's few main controls
         */
        enum class Style { kCompact, kHero };

        Knob();

        void setBipolar(const bool bipolar) { bipolar_ = bipolar; repaint(); }

        void setStyle(Style style);

        void paint(juce::Graphics& g) override;

    protected:
        juce::Rectangle<int> getTextArea() const override;

    private:
        bool bipolar_{false};
        Style style_{Style::kCompact};
        juce::Rectangle<float> getKnobArea() const;
    };

    /**
     * a compact "LABEL  value" field for the footer, edited by dragging
     */
    class ValueField final : public ValueControl {
    public:
        ValueField();

        /**
         * always draw the control background, for fields that stand alone (e.g. in settings)
         */
        void setFramed(const bool framed) { framed_ = framed; repaint(); }

        void paint(juce::Graphics& g) override;

    protected:
        juce::Rectangle<int> getTextArea() const override { return getLocalBounds(); }

    private:
        bool framed_{false};
    };

    /**
     * a compact horizontal slider: caption and value on one line, a thin track below
     * for secondary values next to knobs; can carry an optional toggle chip (e.g. "∞") before the value
     */
    class Slider final : public ValueControl {
    public:
        // clicks on the chip; the owner flips its state and calls setChipOn()
        std::function<void()> on_chip;

        Slider();

        void setBipolar(const bool bipolar) { bipolar_ = bipolar; repaint(); }

        /**
         * a small toggle drawn before the value; when on, the value text is replaced by the chip text
         */
        void setChip(const juce::String& text);

        void setChipOn(bool on);

        void paint(juce::Graphics& g) override;

        void mouseDown(const juce::MouseEvent& e) override;

    protected:
        juce::Rectangle<int> getTextArea() const override;

    private:
        bool bipolar_{false}, chip_on_{false};
        juce::String chip_;
        juce::Rectangle<float> getChipArea() const;
    };

    /**
     * a button that opens a popup menu; shows an optional icon, the current text and a chevron
     */
    class Selector final : public juce::Component, public juce::SettableTooltipClient {
    public:
        std::function<juce::PopupMenu()> build_menu;
        std::function<void(int)> on_result;
        std::function<void(juce::Graphics&, juce::Rectangle<float>, juce::Colour)> draw_icon;

        Selector();

        void setText(const juce::String& text);

        void setLabel(const juce::String& label) { label_ = label; repaint(); }

        void setAccent(const juce::Colour c) { accent_ = c; repaint(); }

        void paint(juce::Graphics& g) override;

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseDown(const juce::MouseEvent&) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection) override;
        void focusLost(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        void showMenu();

    private:
        bool focus_ring_{false};
        juce::String text_, label_;
        juce::Colour accent_;
        bool hovered_{false}, open_{false};
    };

    /**
     * icon-only button; can latch, or report press / release for momentary actions
     */
    class IconButton final : public juce::Component, public juce::SettableTooltipClient {
    public:
        std::function<void()> on_click;
        std::function<void()> on_press;
        std::function<void()> on_release;

        explicit IconButton(icons::Icon icon);

        void setIcon(const icons::Icon icon) { icon_ = icon; repaint(); }

        void setOn(bool on);

        bool isOn() const { return on_; }

        void setOnColour(const juce::Colour c) { on_colour_ = c; repaint(); }

        void setFramed(const bool framed) { framed_ = framed; repaint(); }

        void paint(juce::Graphics& g) override;

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseDown(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection) override;
        void focusLost(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        /**
         * buttons that latch report their state to accessibility clients
         */
        void setToggleable(const bool toggleable) { toggleable_ = toggleable; }

    private:
        bool focus_ring_{false}, toggleable_{false};
        icons::Icon icon_;
        juce::Colour on_colour_;
        bool on_{false}, hovered_{false}, pressed_{false}, framed_{false};
    };

    /**
     * a text button; primary buttons are filled with the accent
     */
    class PillButton final : public juce::Component, public juce::SettableTooltipClient {
    public:
        std::function<void()> on_click;

        explicit PillButton(const juce::String& text, bool primary = false);

        void setText(const juce::String& text) { text_ = text; repaint(); }

        void setPrimary(const bool primary) { primary_ = primary; repaint(); }

        void setDanger(const bool danger) { danger_ = danger; repaint(); }

        int getIdealWidth() const;

        void paint(juce::Graphics& g) override;

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseDown(const juce::MouseEvent&) override { pressed_ = true; repaint(); }
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection) override;
        void focusLost(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        juce::String text_;
        bool primary_, danger_{false}, hovered_{false}, pressed_{false}, focus_ring_{false};
    };

    /**
     * an on/off switch
     */
    class Switch final : public juce::Component {
    public:
        std::function<void(bool)> on_change;

        Switch();

        void setOn(bool on);

        bool isOn() const { return on_; }

        void paint(juce::Graphics& g) override;

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection) override;
        void focusLost(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        void toggle();

    private:
        bool on_{false}, hovered_{false}, focus_ring_{false};
    };

    /**
     * a text pill that toggles, used for analyzer sources and global switches
     */
    class Chip final : public juce::Component, public juce::SettableTooltipClient {
    public:
        std::function<void()> on_click;

        explicit Chip(const juce::String& text = {});

        void setText(const juce::String& text) { text_ = text; repaint(); }

        void setOn(bool on);

        bool isOn() const { return on_; }

        void setOnColour(const juce::Colour c) { on_colour_ = c; repaint(); }

        int getIdealWidth() const;

        void paint(juce::Graphics& g) override;

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection) override;
        void focusLost(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        juce::String text_;
        juce::Colour on_colour_;
        bool on_{false}, hovered_{false}, focus_ring_{false};
    };
}
