// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "widgets.hpp"

#include "../theme/fonts.hpp"
#include "../theme/look_and_feel.hpp"
#include "../theme/tokens.hpp"

namespace altvane::ui {
    using namespace tokens;

    namespace {
        juce::Font captionFont() {
            return Fonts::get().font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f);
        }
    }

    // ---------------------------------------------------------------- ValueControl

    namespace {
        // widgets default to the (user-selectable) accent, resolved at paint time
        juce::Colour resolve(const juce::Colour c) {
            return c.isTransparent() ? colour::kAccent : c;
        }

        void drawFocusRing(juce::Graphics& g, const juce::Rectangle<float> area, const float radius) {
            g.setColour(colour::kAccent.withAlpha(.85f));
            g.drawRoundedRectangle(area.reduced(.75f), radius, 1.5f);
        }

        /**
         * keyboard focus only through Tab, so clicking a control never steals shortcuts from the graph
         */
        void makeKeyboardFocusable(juce::Component& c) {
            c.setWantsKeyboardFocus(true);
            c.setMouseClickGrabsKeyboardFocus(false);
        }

        bool isActivationKey(const juce::KeyPress& key) {
            return key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey;
        }

        class ButtonAccessibility final : public juce::AccessibilityHandler {
        public:
            ButtonAccessibility(juce::Component& c, const juce::AccessibilityRole role, std::function<void()> press,
                                std::function<std::optional<bool>()> state) :
                AccessibilityHandler(c, role, juce::AccessibilityActions().addAction(
                                         juce::AccessibilityActionType::press, std::move(press))),
                state_(std::move(state)) {
            }

            juce::AccessibleState getCurrentState() const override {
                auto s = AccessibilityHandler::getCurrentState();
                if (const auto on = state_ ? state_() : std::nullopt; on.has_value()) {
                    s = s.withCheckable();
                    if (*on) {
                        s = s.withChecked();
                    }
                }
                return s;
            }

        private:
            std::function<std::optional<bool>()> state_;
        };

        class ValueAccessibility final : public juce::AccessibilityValueInterface {
        public:
            explicit ValueAccessibility(ValueControl& control) : control_(control) {
            }

            bool isReadOnly() const override { return !control_.isEnabled(); }

            double getCurrentValue() const override { return control_.getValue(); }

            juce::String getCurrentValueAsString() const override { return control_.format(control_.getValue()); }

            void setValue(const double v) override { control_.setFromUserGesture(static_cast<float>(v)); }

            void setValueAsString(const juce::String& text) override {
                if (control_.parse) {
                    if (const auto v = control_.parse(text)) {
                        control_.setFromUserGesture(*v);
                    }
                }
            }

            AccessibleValueRange getRange() const override {
                const auto& r = control_.getRange();
                return {{r.start, r.end}, r.interval > 0.f ? r.interval : (r.end - r.start) / 100.f};
            }

        private:
            ValueControl& control_;
        };
    }

    ValueControl::ValueControl() {
        setRepaintsOnMouseActivity(false);
        makeKeyboardFocusable(*this);
    }

    std::unique_ptr<juce::AccessibilityHandler> ValueControl::createAccessibilityHandler() {
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
            juce::AccessibilityHandler::Interfaces{std::make_unique<ValueAccessibility>(*this)});
    }

    void ValueControl::setFromUserGesture(const float plain) {
        if (on_begin) {
            on_begin();
        }
        setFromUser(plain);
        if (on_end) {
            on_end();
        }
    }

    void ValueControl::nudge(const float normalised_delta) {
        if (!wheel_active_) {
            wheel_active_ = true;
            if (on_begin) {
                on_begin();
            }
        }
        setFromUser(range_.convertFrom0to1(std::clamp(range_.convertTo0to1(value_) + normalised_delta, 0.f, 1.f)));
        startTimer(450);
    }

    bool ValueControl::keyPressed(const juce::KeyPress& key) {
        const auto step = key.getModifiers().isShiftDown() ? .002f : .01f;
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey) {
            nudge(step);
            return true;
        }
        if (key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey) {
            nudge(-step);
            return true;
        }
        if (key == juce::KeyPress::returnKey && parse) {
            beginTextEdit();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
            resetToDefault();
            return true;
        }
        return false;
    }

    void ValueControl::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        repaint();
    }

    void ValueControl::focusLost(FocusChangeType) {
        focus_ring_ = false;
        repaint();
    }

    void ValueControl::paintFocusRing(juce::Graphics& g, const juce::Rectangle<float> area, const float radius) const {
        if (focus_ring_) {
            drawFocusRing(g, area, radius);
        }
    }

    bool ValueControl::showHostMenu(const juce::MouseEvent& e) {
        // the host's own parameter menu: automation, MIDI learn, etc.
        auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
        if (editor == nullptr || editor->getHostContext() == nullptr) {
            return false;
        }
        const auto idx = editor->getControlParameterIndex(*this);
        const auto& params = editor->getAudioProcessor()->getParameters();
        if (idx < 0 || idx >= params.size()) {
            return false;
        }
        if (auto menu = editor->getHostContext()->getContextMenuForParameter(params[idx])) {
            menu->showNativeMenu(editor->getLocalPoint(this, e.getPosition()));
            return true;
        }
        return false;
    }

    ValueControl::~ValueControl() {
        stopTimer();
    }

    void ValueControl::setRange(const juce::NormalisableRange<float>& range, const float default_plain) {
        range_ = range;
        default_ = default_plain;
        repaint();
    }

    void ValueControl::setValue(const float plain) {
        if (std::abs(plain - value_) > 1e-6f) {
            value_ = plain;
            repaint();
        }
    }

    void ValueControl::setAccent(const juce::Colour c) {
        if (c != accent_) {
            accent_ = c;
            repaint();
        }
    }

    void ValueControl::setLabel(const juce::String& label) {
        label_ = label;
        setTitle(label);
        repaint();
    }

    void ValueControl::setEnabledLook(const bool enabled) {
        if (enabled != enabled_look_) {
            enabled_look_ = enabled;
            setInterceptsMouseClicks(enabled, enabled);
            repaint();
        }
    }

    void ValueControl::setFromUser(const float plain) {
        const auto v = range_.snapToLegalValue(std::clamp(plain, range_.start, range_.end));
        value_ = v;
        if (on_change) {
            on_change(v);
        }
        repaint();
    }

    void ValueControl::resetToDefault() {
        if (on_begin) {
            on_begin();
        }
        setFromUser(default_);
        if (on_end) {
            on_end();
        }
    }

    void ValueControl::mouseEnter(const juce::MouseEvent&) {
        hovered_ = true;
        repaint();
    }

    void ValueControl::mouseExit(const juce::MouseEvent&) {
        hovered_ = false;
        repaint();
    }

    void ValueControl::mouseDown(const juce::MouseEvent& e) {
        if (e.mods.isPopupMenu()) {
            showHostMenu(e);
            return;
        }
        if (e.mods.isCommandDown()) {
            resetToDefault();
            return;
        }
        dragging_ = true;
        drag_norm_ = range_.convertTo0to1(value_);
        drag_start_norm_ = drag_norm_;
        last_drag_y_ = e.position.y;
        if (on_begin) {
            on_begin();
        }
        e.source.enableUnboundedMouseMovement(true, false);
        repaint();
    }

    void ValueControl::mouseDrag(const juce::MouseEvent& e) {
        if (!dragging_) {
            return;
        }
        const auto dy = last_drag_y_ - e.position.y;
        last_drag_y_ = e.position.y;
        const auto fine = e.mods.isShiftDown() ? .15f : 1.f;
        drag_norm_ = std::clamp(drag_norm_ + dy / drag_pixels_ * fine * InteractionSettings::drag_sensitivity, 0.f, 1.f);
        setFromUser(range_.convertFrom0to1(drag_norm_));
    }

    void ValueControl::mouseUp(const juce::MouseEvent& e) {
        if (!dragging_) {
            return;
        }
        dragging_ = false;
        e.source.enableUnboundedMouseMovement(false, true);
        if (on_end) {
            on_end();
        }
        repaint();
    }

    void ValueControl::mouseDoubleClick(const juce::MouseEvent& e) {
        if (getTextArea().contains(e.getPosition()) && parse) {
            beginTextEdit();
        } else {
            resetToDefault();
        }
    }

    void ValueControl::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
        const auto delta = (std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX)
            * (wheel.isReversed ? -1.f : 1.f);
        if (std::abs(delta) < 1e-6f) {
            return;
        }
        if (!wheel_active_) {
            wheel_active_ = true;
            if (on_begin) {
                on_begin();
            }
        }
        const auto fine = e.mods.isShiftDown() ? .2f : 1.f;
        const auto norm = std::clamp(range_.convertTo0to1(value_) + delta * .08f * fine
                                     * InteractionSettings::wheel_sensitivity, 0.f, 1.f);
        setFromUser(range_.convertFrom0to1(norm));
        startTimer(450);
    }

    void ValueControl::timerCallback() {
        stopTimer();
        if (wheel_active_) {
            wheel_active_ = false;
            if (on_end) {
                on_end();
            }
        }
    }

    void ValueControl::beginTextEdit() {
        if (editor_ != nullptr) {
            return;
        }
        editor_ = std::make_unique<juce::TextEditor>();
        editor_->setFont(Fonts::get().font(type::kValue));
        editor_->setJustification(juce::Justification::centred);
        editor_->setIndents(2, 0);
        editor_->setBorder({});
        editor_->setSelectAllWhenFocused(true);
        editor_->addListener(this);
        addAndMakeVisible(*editor_);
        editor_->setBounds(getTextArea().expanded(2, 1));
        editor_->setText(format(value_), false);
        editor_->grabKeyboardFocus();
        editor_->selectAll();
    }

    void ValueControl::closeEditor(const bool commit) {
        if (editor_ == nullptr) {
            return;
        }
        const auto text = editor_->getText();
        auto editor = std::move(editor_);
        editor->removeListener(this);
        removeChildComponent(editor.get());
        if (commit && parse) {
            if (const auto v = parse(text); v.has_value()) {
                if (on_begin) {
                    on_begin();
                }
                setFromUser(*v);
                if (on_end) {
                    on_end();
                }
            }
        }
        repaint();
    }

    void ValueControl::textEditorReturnKeyPressed(juce::TextEditor&) {
        closeEditor(true);
    }

    void ValueControl::textEditorEscapeKeyPressed(juce::TextEditor&) {
        closeEditor(false);
    }

    void ValueControl::textEditorFocusLost(juce::TextEditor&) {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer(this)] {
            if (safe != nullptr) {
                safe->closeEditor(true);
            }
        });
    }

    // ---------------------------------------------------------------- Knob

    Knob::Knob() {
        drag_pixels_ = 240.f;
    }

    juce::Rectangle<float> Knob::getKnobArea() const {
        auto b = getLocalBounds().toFloat();
        b.removeFromTop(16.f);
        b.removeFromBottom(20.f);
        const auto d = std::min({b.getWidth(), b.getHeight(), 46.f});
        return juce::Rectangle<float>(d, d).withCentre(b.getCentre());
    }

    juce::Rectangle<int> Knob::getTextArea() const {
        return getLocalBounds().removeFromBottom(18).reduced(2, 0);
    }

    void Knob::paint(juce::Graphics& g) {
        const auto alpha = enabled_look_ ? 1.f : .35f;
        auto bounds = getLocalBounds().toFloat();

        // caption
        g.setColour(colour::kTextTertiary.withMultipliedAlpha(alpha));
        g.setFont(captionFont());
        g.drawText(label_.toUpperCase(), bounds.removeFromTop(14.f), juce::Justification::centred);

        // rotary
        const auto area = getKnobArea();
        const auto centre = area.getCentre();
        const auto radius = area.getWidth() * .5f;
        constexpr auto kStart = juce::MathConstants<float>::pi * -.75f;
        constexpr auto kEnd = juce::MathConstants<float>::pi * .75f;
        const auto norm = getNormalised();
        const auto angle = kStart + norm * (kEnd - kStart);

        const auto arc_r = radius - 1.5f;
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arc_r, arc_r, 0.f, kStart, kEnd, true);
        g.setColour(colour::kHairlineStrong.withMultipliedAlpha(alpha));
        g.strokePath(track, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value_arc;
        const auto from = bipolar_ ? 0.f : kStart;
        value_arc.addCentredArc(centre.x, centre.y, arc_r, arc_r, 0.f, std::min(from, angle), std::max(from, angle),
                                true);
        g.setColour(resolve(accent_).withMultipliedAlpha(alpha));
        g.strokePath(value_arc, juce::PathStrokeType(2.f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        // body
        const auto body = area.reduced(6.f);
        const auto body_colour = (hovered_ || dragging_) ? colour::kControlHover : colour::kControl;
        g.setGradientFill(juce::ColourGradient(body_colour.brighter(.12f), body.getCentreX(), body.getY(),
                                               body_colour.darker(.15f), body.getCentreX(), body.getBottom(), false));
        g.fillEllipse(body);
        g.setColour(juce::Colours::white.withAlpha(.06f));
        g.drawEllipse(body.reduced(.5f), 1.f);

        // indicator
        const auto inner = body.getWidth() * .5f;
        const juce::Point<float> p1{centre.x + std::sin(angle) * inner * .35f, centre.y - std::cos(angle) * inner * .35f};
        const juce::Point<float> p2{centre.x + std::sin(angle) * inner * .78f, centre.y - std::cos(angle) * inner * .78f};
        g.setColour(colour::kTextPrimary.withMultipliedAlpha(alpha));
        g.drawLine({p1, p2}, 1.8f);

        paintFocusRing(g, area.expanded(3.f), area.getWidth() * .5f + 3.f);

        // value
        const auto value_colour = dragging_ ? colour::kTextPrimary : colour::kTextPrimary.withMultipliedAlpha(.85f);
        g.setColour(value_colour.withMultipliedAlpha(alpha));
        Fonts::get().drawTabular(g, format(value_), getTextArea().toFloat(), juce::Justification::centred,
                                 type::kBody);
    }

    // ---------------------------------------------------------------- ValueField

    ValueField::ValueField() {
        drag_pixels_ = 300.f;
        // the accent colours the caption, so a field can signal a mode (e.g. loudness matching)
        accent_ = colour::kTextTertiary;
    }

    void ValueField::paint(juce::Graphics& g) {
        auto b = getLocalBounds().toFloat();
        if (framed_ || hovered_ || dragging_) {
            g.setColour(framed_ && (hovered_ || dragging_) ? colour::kControlHover : colour::kControl);
            g.fillRoundedRectangle(b, size::kRadiusSmall);
        }
        b.reduce(8.f, 0.f);
        paintFocusRing(g, getLocalBounds().toFloat(), size::kRadiusSmall);
        if (label_.isEmpty()) {
            g.setColour(dragging_ ? colour::kTextPrimary : colour::kTextSecondary);
            Fonts::get().drawTabular(g, format(value_), b, juce::Justification::centredLeft, type::kBody);
            return;
        }
        g.setColour(accent_);
        g.setFont(captionFont());
        const auto label_w = juce::GlyphArrangement::getStringWidth(captionFont(), label_.toUpperCase()) + 6.f;
        g.drawText(label_.toUpperCase(), b.removeFromLeft(label_w), juce::Justification::centredLeft);
        g.setColour(dragging_ ? colour::kTextPrimary : colour::kTextSecondary);
        Fonts::get().drawTabular(g, format(value_), b, juce::Justification::centredRight, type::kBody);
    }

    // ---------------------------------------------------------------- Selector

    Selector::Selector() : accent_(colour::kTextSecondary) {
        makeKeyboardFocusable(*this);
    }

    std::unique_ptr<juce::AccessibilityHandler> Selector::createAccessibilityHandler() {
        return std::make_unique<ButtonAccessibility>(*this, juce::AccessibilityRole::comboBox,
                                                     [this] { showMenu(); }, nullptr);
    }

    bool Selector::keyPressed(const juce::KeyPress& key) {
        if (isActivationKey(key) || key == juce::KeyPress::downKey) {
            showMenu();
            return true;
        }
        return false;
    }

    void Selector::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        repaint();
    }

    void Selector::focusLost(FocusChangeType) {
        focus_ring_ = false;
        repaint();
    }

    void Selector::setText(const juce::String& text) {
        if (text != text_) {
            text_ = text;
            setTitle(label_ + " " + text);
            repaint();
        }
    }

    void Selector::paint(juce::Graphics& g) {
        auto b = getLocalBounds().toFloat();
        g.setColour(hovered_ || open_ ? colour::kControlHover : colour::kControl);
        g.fillRoundedRectangle(b, size::kRadiusSmall);
        b.reduce(8.f, 0.f);
        icons::draw(g, icons::Icon::kChevronDown, b.removeFromRight(10.f), colour::kTextTertiary, 1.3f);
        b.removeFromRight(4.f);
        if (draw_icon) {
            auto icon_area = b.removeFromLeft(b.getHeight() * 1.1f).reduced(1.f, b.getHeight() * .22f);
            draw_icon(g, icon_area, accent_);
            b.removeFromLeft(6.f);
        }
        g.setColour(colour::kTextPrimary);
        g.setFont(Fonts::get().font(type::kBody));
        g.drawText(text_, b, juce::Justification::centredLeft, true);
        if (focus_ring_) {
            drawFocusRing(g, getLocalBounds().toFloat(), size::kRadiusSmall);
        }
    }

    void Selector::mouseDown(const juce::MouseEvent&) {
        showMenu();
    }

    void Selector::showMenu() {
        if (!build_menu) {
            return;
        }
        open_ = true;
        repaint();
        showPopupMenu(build_menu(), *this, juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth())
                                                     .withStandardItemHeight(24),
                           [safe = juce::Component::SafePointer(this)](const int result) {
                               if (safe == nullptr) {
                                   return;
                               }
                               safe->open_ = false;
                               safe->repaint();
                               if (result != 0 && safe->on_result) {
                                   safe->on_result(result);
                               }
                           });
    }

    // ---------------------------------------------------------------- IconButton

    IconButton::IconButton(const icons::Icon icon) : icon_(icon) {
        makeKeyboardFocusable(*this);
    }

    std::unique_ptr<juce::AccessibilityHandler> IconButton::createAccessibilityHandler() {
        if (getTitle().isEmpty()) {
            // icon-only: the tooltip is the best accessible name
            setTitle(getTooltip());
        }
        return std::make_unique<ButtonAccessibility>(
            *this, toggleable_ ? juce::AccessibilityRole::toggleButton : juce::AccessibilityRole::button,
            [this] {
                if (on_press) {
                    on_press();
                }
                if (on_release) {
                    on_release();
                }
                if (on_click) {
                    on_click();
                }
            },
            [this]() -> std::optional<bool> {
                return toggleable_ ? std::optional<bool>{on_} : std::nullopt;
            });
    }

    bool IconButton::keyPressed(const juce::KeyPress& key) {
        if (isActivationKey(key)) {
            if (on_press) {
                on_press();
            }
            if (on_release) {
                on_release();
            }
            if (on_click) {
                on_click();
            }
            return true;
        }
        return false;
    }

    void IconButton::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        repaint();
    }

    void IconButton::focusLost(FocusChangeType) {
        focus_ring_ = false;
        repaint();
    }

    void IconButton::setOn(const bool on) {
        if (on != on_) {
            on_ = on;
            repaint();
        }
    }

    void IconButton::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        if (framed_ || hovered_ || pressed_ || on_) {
            auto bg = hovered_ || pressed_ ? colour::kControlHover : colour::kControl;
            if (on_) {
                bg = resolve(on_colour_).withAlpha(.16f);
            }
            g.setColour(bg);
            g.fillRoundedRectangle(b, size::kRadiusSmall);
        }
        const auto c = on_ ? resolve(on_colour_) : (hovered_ ? colour::kTextPrimary : colour::kTextSecondary);
        const auto side = std::min(b.getWidth(), b.getHeight()) * .62f;
        icons::draw(g, icon_, juce::Rectangle<float>(side, side).withCentre(b.getCentre()), c, 1.4f);
        if (focus_ring_) {
            drawFocusRing(g, b, size::kRadiusSmall);
        }
    }

    void IconButton::mouseDown(const juce::MouseEvent&) {
        pressed_ = true;
        if (on_press) {
            on_press();
        }
        repaint();
    }

    void IconButton::mouseUp(const juce::MouseEvent& e) {
        pressed_ = false;
        if (on_release) {
            on_release();
        }
        if (on_click && getLocalBounds().contains(e.getPosition())) {
            on_click();
        }
        repaint();
    }

    // ---------------------------------------------------------------- PillButton

    PillButton::PillButton(const juce::String& text, const bool primary) : text_(text), primary_(primary) {
        setTitle(text);
        makeKeyboardFocusable(*this);
    }

    std::unique_ptr<juce::AccessibilityHandler> PillButton::createAccessibilityHandler() {
        return std::make_unique<ButtonAccessibility>(*this, juce::AccessibilityRole::button, [this] {
            if (on_click && isEnabled()) {
                on_click();
            }
        }, nullptr);
    }

    bool PillButton::keyPressed(const juce::KeyPress& key) {
        if (isActivationKey(key) && on_click && isEnabled()) {
            on_click();
            return true;
        }
        return false;
    }

    void PillButton::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        repaint();
    }

    void PillButton::focusLost(FocusChangeType) {
        focus_ring_ = false;
        repaint();
    }

    int PillButton::getIdealWidth() const {
        return juce::roundToInt(juce::GlyphArrangement::getStringWidth(
            Fonts::get().font(type::kBody, Fonts::kSemiBold), text_)) + 28;
    }

    void PillButton::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        const auto enabled = isEnabled();
        juce::Colour bg, fg;
        if (danger_) {
            bg = colour::kDanger.withAlpha(hovered_ ? .9f : .75f);
            fg = juce::Colours::white;
        } else if (primary_) {
            bg = colour::kAccent.withMultipliedBrightness(hovered_ ? 1.08f : 1.f);
            fg = colour::kWindow;
        } else {
            bg = hovered_ ? colour::kControlHover : colour::kControl;
            fg = colour::kTextPrimary;
        }
        if (pressed_) {
            bg = bg.darker(.15f);
        }
        g.setColour(bg.withMultipliedAlpha(enabled ? 1.f : .4f));
        g.fillRoundedRectangle(b, size::kRadiusSmall);
        g.setColour(fg.withMultipliedAlpha(enabled ? 1.f : .5f));
        g.setFont(Fonts::get().font(type::kBody, Fonts::kSemiBold));
        g.drawText(text_, b, juce::Justification::centred);
        if (focus_ring_) {
            drawFocusRing(g, b, size::kRadiusSmall);
        }
    }

    void PillButton::mouseUp(const juce::MouseEvent& e) {
        pressed_ = false;
        repaint();
        if (on_click && isEnabled() && getLocalBounds().contains(e.getPosition())) {
            on_click();
        }
    }

    // ---------------------------------------------------------------- Switch

    Switch::Switch() {
        makeKeyboardFocusable(*this);
    }

    std::unique_ptr<juce::AccessibilityHandler> Switch::createAccessibilityHandler() {
        return std::make_unique<ButtonAccessibility>(*this, juce::AccessibilityRole::toggleButton,
                                                     [this] { toggle(); },
                                                     [this]() -> std::optional<bool> { return on_; });
    }

    void Switch::toggle() {
        setOn(!on_);
        if (on_change) {
            on_change(on_);
        }
    }

    bool Switch::keyPressed(const juce::KeyPress& key) {
        if (isActivationKey(key)) {
            toggle();
            return true;
        }
        return false;
    }

    void Switch::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        repaint();
    }

    void Switch::focusLost(FocusChangeType) {
        focus_ring_ = false;
        repaint();
    }

    void Switch::setOn(const bool on) {
        if (on != on_) {
            on_ = on;
            repaint();
        }
    }

    void Switch::paint(juce::Graphics& g) {
        const auto track = juce::Rectangle<float>(30.f, 18.f).withCentre(getLocalBounds().toFloat().getCentre())
                                                             .withRightX(static_cast<float>(getWidth()));
        g.setColour(on_ ? colour::kAccent : (hovered_ ? colour::kControlHover : colour::kControl));
        g.fillRoundedRectangle(track, 9.f);
        const auto knob = juce::Rectangle<float>(14.f, 14.f).withCentre({
            on_ ? track.getRight() - 9.f : track.getX() + 9.f, track.getCentreY()
        });
        g.setColour(on_ ? colour::kWindow : colour::kTextSecondary);
        g.fillEllipse(knob);
        if (focus_ring_) {
            drawFocusRing(g, track.expanded(3.f), 12.f);
        }
    }

    void Switch::mouseUp(const juce::MouseEvent& e) {
        if (getLocalBounds().contains(e.getPosition())) {
            toggle();
        }
    }

    // ---------------------------------------------------------------- Chip

    Chip::Chip(const juce::String& text) : text_(text) {
        setTitle(text);
        makeKeyboardFocusable(*this);
    }

    std::unique_ptr<juce::AccessibilityHandler> Chip::createAccessibilityHandler() {
        return std::make_unique<ButtonAccessibility>(*this, juce::AccessibilityRole::toggleButton, [this] {
            if (on_click) {
                on_click();
            }
        }, [this]() -> std::optional<bool> { return on_; });
    }

    bool Chip::keyPressed(const juce::KeyPress& key) {
        if (isActivationKey(key) && on_click) {
            on_click();
            return true;
        }
        return false;
    }

    void Chip::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        repaint();
    }

    void Chip::focusLost(FocusChangeType) {
        focus_ring_ = false;
        repaint();
    }

    void Chip::setOn(const bool on) {
        if (on != on_) {
            on_ = on;
            repaint();
        }
    }

    int Chip::getIdealWidth() const {
        return juce::roundToInt(juce::GlyphArrangement::getStringWidth(Fonts::get().font(type::kSmall), text_)) + 20;
    }

    void Chip::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        if (on_) {
            g.setColour(resolve(on_colour_).withAlpha(.15f));
            g.fillRoundedRectangle(b, size::kRadiusSmall);
        } else {
            g.setColour(hovered_ ? colour::kControlHover : colour::kControl.withAlpha(.55f));
            g.fillRoundedRectangle(b, size::kRadiusSmall);
        }
        g.setColour(on_ ? resolve(on_colour_) : (hovered_ ? colour::kTextPrimary : colour::kTextSecondary));
        g.setFont(Fonts::get().font(type::kSmall, on_ ? Fonts::kSemiBold : Fonts::kRegular));
        g.drawText(text_, b, juce::Justification::centred);
        if (focus_ring_) {
            drawFocusRing(g, b, size::kRadiusSmall);
        }
    }

    void Chip::mouseUp(const juce::MouseEvent& e) {
        if (on_click && getLocalBounds().contains(e.getPosition())) {
            on_click();
        }
    }
}
