// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "control_card.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "../model/formats.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;

    namespace {
        constexpr std::array kDirectionNames{"Compress", "Inflate", "Expand", "Shape"};
        constexpr std::array kDirectionHints{
            "Turns loud parts down", "Brings quiet parts up", "Turns quiet parts down", "Brings loud parts up"
        };

        // each direction's curve on a 16 x 16 grid, over a faint 1:1 diagonal
        juce::Path directionIcon(const int index) {
            static constexpr std::array<std::array<float, 6>, 4> kPoints{
                {
                    {2.f, 14.f, 8.f, 8.f, 14.f, 6.f},
                    {2.f, 10.f, 8.f, 8.f, 14.f, 2.f},
                    {2.f, 15.f, 7.f, 10.f, 14.f, 3.f},
                    {2.f, 14.f, 8.f, 8.f, 14.f, 1.f},
                }
            };
            const auto& p = kPoints[static_cast<size_t>(index)];
            juce::Path path;
            path.startNewSubPath(p[0], p[1]);
            path.lineTo(p[2], p[3]);
            path.lineTo(p[4], p[5]);
            return path;
        }

        juce::Font captionFont() {
            return Fonts::get().font(type::kCaption, Fonts::kSemiBold).withExtraKerningFactor(.08f);
        }
    }

    // ---------------------------------------------------------------- DirectionPicker

    class DirectionPicker::Segment final : public juce::Component, public juce::SettableTooltipClient {
    public:
        Segment(DirectionPicker& owner, const int index) : owner_(owner), index_(index) {
            setTitle(kDirectionNames[static_cast<size_t>(index)]);
            setTooltip(juce::String(kDirectionNames[static_cast<size_t>(index)]) + ": "
                       + kDirectionHints[static_cast<size_t>(index)]);
            setWantsKeyboardFocus(true);
            setMouseClickGrabsKeyboardFocus(false);
        }

        void paint(juce::Graphics& g) override {
            const auto b = getLocalBounds().toFloat();
            const auto on = owner_.selected_ == index_;
            if (on) {
                g.setColour(colour::kAccent.withAlpha(.15f));
                g.fillRoundedRectangle(b, 3.f);
            } else if (hovered_) {
                g.setColour(colour::kControlHover);
                g.fillRoundedRectangle(b, 3.f);
            }
            const auto icon = juce::Rectangle<float>(16.f, 16.f).withCentre(b.getCentre());
            const auto t = juce::AffineTransform::translation(icon.getX(), icon.getY());
            const auto c = on ? colour::kAccent : hovered_ ? colour::kTextPrimary : colour::kTextTertiary;
            juce::Path diagonal;
            diagonal.startNewSubPath(2.f, 14.f);
            diagonal.lineTo(14.f, 2.f);
            g.setColour(c.withMultipliedAlpha(.3f));
            g.strokePath(diagonal, juce::PathStrokeType(1.f), t);
            g.setColour(c);
            g.strokePath(directionIcon(index_), juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                                     juce::PathStrokeType::rounded), t);
            if (focus_ring_) {
                g.setColour(colour::kAccent.withAlpha(.85f));
                g.drawRoundedRectangle(b.reduced(.75f), 3.f, 1.5f);
            }
        }

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }

        void mouseUp(const juce::MouseEvent& e) override {
            if (getLocalBounds().contains(e.getPosition())) {
                select();
            }
        }

        bool keyPressed(const juce::KeyPress& key) override {
            if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey) {
                select();
                return true;
            }
            return false;
        }

        void focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) override {
            focus_ring_ = cause == focusChangedByTabKey;
            repaint();
        }

        void focusLost(FocusChangeType) override {
            focus_ring_ = false;
            repaint();
        }

        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            class Handler final : public juce::AccessibilityHandler {
            public:
                explicit Handler(Segment& s) :
                    AccessibilityHandler(s, juce::AccessibilityRole::radioButton,
                                         juce::AccessibilityActions().addAction(
                                             juce::AccessibilityActionType::press, [&s] { s.select(); })),
                    segment_(s) {
                }

                juce::AccessibleState getCurrentState() const override {
                    auto state = AccessibilityHandler::getCurrentState().withCheckable();
                    return segment_.owner_.selected_ == segment_.index_ ? state.withChecked() : state;
                }

            private:
                Segment& segment_;
            };
            return std::make_unique<Handler>(*this);
        }

    private:
        DirectionPicker& owner_;
        int index_;
        bool hovered_{false}, focus_ring_{false};

        void select() const {
            if (owner_.on_change) {
                owner_.on_change(index_);
            }
        }
    };

    DirectionPicker::DirectionPicker() {
        setTitle("Direction");
        setFocusContainerType(FocusContainerType::focusContainer);
        for (int i = 0; i < 4; ++i) {
            segments_.push_back(std::make_unique<Segment>(*this, i));
            addAndMakeVisible(*segments_.back());
        }
    }

    void DirectionPicker::setSelected(const int index) {
        if (index != selected_) {
            selected_ = index;
            for (auto& s : segments_) {
                s->repaint();
            }
        }
    }

    void DirectionPicker::resized() {
        auto b = getLocalBounds().reduced(2);
        const auto w = (b.getWidth() - 6) / 4;
        for (auto& s : segments_) {
            s->setBounds(b.removeFromLeft(w));
            b.removeFromLeft(2);
        }
    }

    void DirectionPicker::paint(juce::Graphics& g) {
        g.setColour(colour::kControl.withAlpha(.55f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), size::kRadiusSmall);
    }

    // ---------------------------------------------------------------- ControlCard

    ControlCard::ControlCard(CompBridge& bridge) : bridge_(bridge) {
        setTitle("Controls");
        setFocusContainerType(FocusContainerType::focusContainer);

        style_.setComponentID(zlp::PCompStyle::kID);
        style_.setTitle("Style");
        style_.setTooltip("How the detector and envelope behave. Changes the feel, not the settings.");
        style_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = bridge_.getChoice(zlp::PCompStyle::kID);
            const auto d = bridge_.getDirection();
            const auto forced = d == zlp::PCompDirection::kInflate || d == zlp::PCompDirection::kExpand;
            for (int i = 0; i < zlp::PCompStyle::kChoices.size(); ++i) {
                juce::PopupMenu::Item item(zlp::PCompStyle::kChoices[i]);
                item.itemID = i + 1;
                item.isTicked = i == current;
                if (forced && i > 1) {
                    item.text << "  (Clean when " << (d == zlp::PCompDirection::kInflate ? "inflating)" : "expanding)");
                }
                menu.addItem(item);
            }
            return menu;
        };
        style_.on_result = [this](const int id) {
            bridge_.set(zlp::PCompStyle::kID, static_cast<float>(id - 1), "Style");
        };

        direction_.setComponentID(zlp::PCompDirection::kID);
        direction_.on_change = [this](const int index) {
            bridge_.set(zlp::PCompDirection::kID, static_cast<float>(index), "Direction");
        };

        more_.setTooltip("Detection, envelope shaping and the output clipper");
        more_.on_click = [this] {
            if (on_more) {
                on_more();
            }
        };

        const auto knob = [this](ui::Knob& k, const char* id, const juce::String& name,
                                 std::function<juce::String(float)> format) {
            ui::bindValue(k, bridge_.getEditor(), bridge_.para(id), name);
            k.format = std::move(format);
            addAndMakeVisible(k);
        };
        const auto slider = [this](ui::Slider& s, const char* id, const juce::String& name,
                                   std::function<juce::String(float)> format) {
            ui::bindValue(s, bridge_.getEditor(), bridge_.para(id), name);
            s.format = std::move(format);
            addAndMakeVisible(s);
        };
        knob(threshold_, zlp::PThreshold::kID, "Threshold", formats::db);
        knob(ratio_, zlp::PRatio::kID, "Ratio", formats::ratio);
        slider(knee_, zlp::PKneeW::kID, "Knee", formats::db);
        slider(range_, zlp::PRange::kID, "Range", formats::db);
        knob(attack_, zlp::PAttack::kID, "Attack", formats::ms);
        knob(release_, zlp::PRelease::kID, "Release", formats::ms);
        slider(hold_, zlp::PHold::kID, "Hold", formats::ms);
        slider(lookahead_, zlp::PLookAhead::kID, "Lookahead", formats::ms);
        knob(makeup_, zlp::POutGain::kID, "Makeup", formats::signedDB);
        knob(wet_, zlp::PWet::kID, "Wet", formats::percent);

        for (auto* s : {&attack_, &release_}) {
            s->parse = formats::parseMs;
        }
        for (auto* s : {&hold_, &lookahead_}) {
            s->parse = formats::parseMs;
        }
        ratio_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = ui::format::parseNumber(t.upToFirstOccurrenceOf(":", false, false))) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        lookahead_.setBipolar(true);
        makeup_.setBipolar(true);
        range_.setChip(juce::String::fromUTF8("\xe2\x88\x9e"));
        range_.on_chip = [this] { bridge_.toggle(zlp::PRangeINF::kID, "Infinite Range"); };

        threshold_.setTooltip("Level where the gain change starts. Also drag the line on the display.");
        ratio_.setTooltip("How strongly the level beyond the threshold is changed. 100:1 behaves like a limiter.");
        knee_.setTooltip("Half-width of the soft knee around the threshold. 0 dB is a hard knee.");
        range_.setTooltip("The most gain the compressor may change. Turn on " + juce::String::fromUTF8("\xe2\x88\x9e")
                          + " for no limit.");
        attack_.setTooltip("How fast the gain change reacts once the level crosses the threshold.");
        release_.setTooltip("How fast the gain change recovers after the level drops.");
        hold_.setTooltip("Keeps the gain change steady for this long before release starts.");
        lookahead_.setTooltip("Positive values delay the audio so the detector reacts early; negative values delay "
            "the side chain. Adds latency.");
        makeup_.setTooltip("Output gain after compression. Learn sets it to match the input loudness.");
        wet_.setTooltip("Scales the whole gain change. Below 100% is parallel compression.");

        learn_.setOnColour(colour::kWarning);
        learn_.setTooltip("Play the loudest part, then click again: makeup is set so the output matches the input "
            "loudness.");
        learn_.on_click = [this] { toggleLearn(); };

        addAndMakeVisible(style_);
        addAndMakeVisible(direction_);
        addAndMakeVisible(more_);
        addAndMakeVisible(learn_);
    }

    ControlCard::~ControlCard() {
        if (learning_) {
            bridge_.getProcessor().getCompressController().setLUFSMatcherOn(false);
        }
    }

    void ControlCard::toggleLearn() {
        auto& controller = bridge_.getProcessor().getCompressController();
        learning_ = !learning_;
        controller.setLUFSMatcherOn(learning_);
        learn_.setOn(learning_);
        learn_.setText(learning_ ? juce::String::fromUTF8("Learning\xe2\x80\xa6") : "Learn");
        if (learning_) {
            return;
        }
        // as upstream: the loudness difference, undone by the wet amount the engine scales makeup with
        const auto wet = bridge_.get(zlp::PWet::kID);
        if (wet < .1f) {
            return;
        }
        const auto diff = -controller.getLUFSMatcherDiff();
        const auto target = std::clamp(bridge_.get(zlp::POutGain::kID) + diff / (wet * .01f), -30.f, 30.f);
        bridge_.set(zlp::POutGain::kID, target, "Makeup (learned)");
    }

    juce::String ControlCard::getStyleText() const {
        const auto name = zlp::PCompStyle::kChoices[bridge_.getChoice(zlp::PCompStyle::kID)];
        return bridge_.isStyleForcedClean() ? name + juce::String::fromUTF8(" \xe2\x86\x92 Clean") : name;
    }

    void ControlCard::tick(const double dt, const std::optional<float> side_db) {
        threshold_.setValue(bridge_.get(zlp::PThreshold::kID));
        ratio_.setValue(bridge_.get(zlp::PRatio::kID));
        knee_.setValue(bridge_.get(zlp::PKneeW::kID));
        range_.setValue(bridge_.get(zlp::PRange::kID));
        range_.setChipOn(bridge_.getBool(zlp::PRangeINF::kID));
        attack_.setValue(bridge_.get(zlp::PAttack::kID));
        release_.setValue(bridge_.get(zlp::PRelease::kID));
        hold_.setValue(bridge_.get(zlp::PHold::kID));
        lookahead_.setValue(bridge_.get(zlp::PLookAhead::kID));
        makeup_.setValue(bridge_.get(zlp::POutGain::kID));
        wet_.setValue(bridge_.get(zlp::PWet::kID));
        style_.setText(getStyleText());
        direction_.setSelected(bridge_.getChoice(zlp::PCompDirection::kID));

        // the threshold knob rings the detector level, on the threshold's own scale
        const auto range = bridge_.para(zlp::PThreshold::kID)->getNormalisableRange();
        const auto level = side_db.has_value() && *side_db > -100.f
                               ? range.convertTo0to1(std::clamp(*side_db, -100.f, 0.f))
                               : 0.f;
        const auto ring = std::max(level, ring_ - static_cast<float>(dt) * .8f);
        if (std::abs(ring - ring_) > 1e-4f) {
            ring_ = ring;
            repaint(threshold_.getBounds().expanded(4));
        }
        repaint(direction_name_.toNearestInt());
    }

    void ControlCard::resized() {
        auto b = getLocalBounds();
        b.removeFromLeft(14);
        b.removeFromRight(14);
        b.removeFromTop(12);
        b.removeFromBottom(10);

        // style and direction
        auto mode = b.removeFromLeft(136);
        style_caption_ = mode.removeFromTop(14).toFloat();
        style_.setBounds(mode.removeFromTop(26));
        mode.removeFromTop(6);
        direction_.setBounds(mode.removeFromTop(26));
        mode.removeFromTop(6);
        auto row = mode.removeFromTop(22);
        more_.setBounds(row.removeFromRight(more_.getIdealWidth() + 4));
        direction_name_ = row.toFloat();

        const auto divider = [&](const size_t i) {
            dividers_[i] = static_cast<float>(b.getX()) + 10.f;
            b.removeFromLeft(21);
        };
        const auto knobs = [&](ui::Knob& a, ui::Knob& c) {
            a.setBounds(b.removeFromLeft(size::kKnobWidth).withHeight(size::kKnobHeight));
            b.removeFromLeft(6);
            c.setBounds(b.removeFromLeft(size::kKnobWidth).withHeight(size::kKnobHeight));
            b.removeFromLeft(6);
        };
        // the sliders give way when the window is narrow
        const auto fixed = 3 * 21 + 6 * size::kKnobWidth + 5 * 6;
        const auto slider_w = std::clamp((b.getWidth() - fixed) / 2, 92, size::kSliderWidth);
        const auto sliders = [&](ui::Slider& a, ui::Slider& c) {
            auto col = b.removeFromLeft(slider_w);
            col.removeFromTop(14);
            a.setBounds(col.removeFromTop(size::kSliderHeight));
            col.removeFromTop(6);
            c.setBounds(col.removeFromTop(size::kSliderHeight));
        };
        divider(0);
        knobs(threshold_, ratio_);
        sliders(knee_, range_);
        divider(1);
        knobs(attack_, release_);
        sliders(hold_, lookahead_);
        divider(2);
        auto makeup = b.removeFromLeft(size::kKnobWidth);
        makeup_.setBounds(makeup.removeFromTop(size::kKnobHeight));
        makeup.removeFromTop(2);
        learn_.setBounds(makeup.removeFromTop(18).withSizeKeepingCentre(learn_.getIdealWidth() + 16, 18));
        b.removeFromLeft(6);
        wet_.setBounds(b.removeFromLeft(size::kKnobWidth).withHeight(size::kKnobHeight));
    }

    void ControlCard::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kGlass);
        g.fillRoundedRectangle(b, 12.f);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(b.reduced(.5f), 12.f, 1.f);

        g.setColour(colour::kHairline);
        for (const auto x : dividers_) {
            g.fillRect(juce::Rectangle<float>(x, 16.f, 1.f, b.getHeight() - 26.f));
        }
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("STYLE", style_caption_, juce::Justification::centredLeft);
        g.setFont(Fonts::get().font(type::kSmall));
        g.setColour(colour::kTextSecondary);
        g.drawText(kDirectionNames[static_cast<size_t>(std::clamp(bridge_.getChoice(zlp::PCompDirection::kID), 0, 3))],
                   direction_name_, juce::Justification::centredLeft);
    }

    void ControlCard::paintOverChildren(juce::Graphics& g) {
        if (ring_ <= 0.f) {
            return;
        }
        // the same geometry as the knob's arc, one ring further out
        auto area = threshold_.getBounds().toFloat();
        area.removeFromTop(16.f);
        area.removeFromBottom(20.f);
        const auto d = std::min({area.getWidth(), area.getHeight(), 46.f});
        const auto centre = area.getCentre();
        const auto r = d * .5f + 3.f;
        constexpr auto kStart = juce::MathConstants<float>::pi * -.75f;
        constexpr auto kEnd = juce::MathConstants<float>::pi * .75f;
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, r, r, 0.f, kStart, kStart + ring_ * (kEnd - kStart), true);
        g.setColour(colour::kSpectrumSide.withAlpha(.45f));
        g.strokePath(arc, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
}
