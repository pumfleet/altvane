// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "display_buttons.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/widgets/help_balloon.hpp"
#include "altvane_ui/widgets/icons.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;

    namespace {
        juce::Font labelFont() {
            return Fonts::get().font(type::kSmall, Fonts::kSemiBold).withExtraKerningFactor(.06f);
        }
    }

    DisplayButton::DisplayButton(const Kind kind) : kind_(kind) {
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        if (kind == Kind::kKneeTab) {
            setTitle("Knee display");
            ui::setHelp(*this, "Knee display", "Show or hide the knee display.");
        } else {
            setTitle("Side chain");
            ui::setHelp(*this, "Side chain", "What the detector listens to: source, stereo handling and its own EQ. "
                        "Glows when it changes the sound.");
        }
    }

    void DisplayButton::setOpen(const bool open) {
        if (open != open_) {
            open_ = open;
            repaint();
        }
    }

    void DisplayButton::setActive(const bool active) {
        if (active != active_) {
            active_ = active;
            repaint();
        }
    }

    int DisplayButton::getIdealWidth() const {
        if (kind_ == Kind::kKneeTab) {
            return 18;
        }
        return juce::roundToInt(juce::GlyphArrangement::getStringWidth(labelFont(), "SIDE CHAIN")) + 46;
    }

    void DisplayButton::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        const auto text_colour = hovered_ ? colour::kTextPrimary : colour::kTextSecondary;
        if (kind_ == Kind::kKneeTab) {
            juce::Path tab;
            tab.addRoundedRectangle(b.getX(), b.getY(), b.getWidth() + 6.f, b.getHeight(), 6.f, 6.f,
                                    true, false, true, false);
            g.setColour(hovered_ ? colour::kControl : colour::kRaised);
            g.fillPath(tab);
            g.setColour(colour::kHairlineStrong);
            g.strokePath(tab, juce::PathStrokeType(1.f));
            juce::Graphics::ScopedSaveState state(g);
            g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, b.getCentreX(),
                                                           b.getCentreY()));
            g.setFont(Fonts::get().font(9.f, Fonts::kSemiBold).withExtraKerningFactor(.14f));
            g.setColour(open_ ? colour::kAccent : text_colour);
            g.drawText("KNEE", juce::Rectangle<float>(b.getHeight(), b.getWidth()).withCentre(b.getCentre()),
                       juce::Justification::centred);
        } else {
            const auto pill = b.reduced(.5f);
            g.setColour(colour::kGlass);
            g.fillRoundedRectangle(pill, pill.getHeight() * .5f);
            g.setColour(active_ ? colour::kWarning.withAlpha(.4f) : colour::kHairlineStrong);
            g.drawRoundedRectangle(pill, pill.getHeight() * .5f, 1.f);

            auto row = b.reduced(10.f, 0.f);
            const auto dot = row.removeFromLeft(6.f).withSizeKeepingCentre(6.f, 6.f);
            if (active_) {
                g.setColour(colour::kWarning.withAlpha(.3f));
                g.fillEllipse(dot.expanded(3.f));
            }
            g.setColour(active_ ? colour::kWarning : colour::kTextDisabled);
            g.fillEllipse(dot);
            row.removeFromLeft(7.f);
            const auto chevron = row.removeFromRight(10.f);
            g.setFont(labelFont());
            g.setColour(active_ ? colour::kWarning : text_colour);
            g.drawText("SIDE CHAIN", row, juce::Justification::centredLeft);
            // the drawer rises from below: the chevron points up while it is closed
            const auto chevron_area = chevron.withSizeKeepingCentre(10.f, 10.f);
            juce::Graphics::ScopedSaveState state(g);
            if (!open_) {
                g.addTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi,
                                                               chevron_area.getCentreX(), chevron_area.getCentreY()));
            }
            ui::icons::draw(g, ui::icons::Icon::kChevronDown, chevron_area, active_ ? colour::kWarning : text_colour,
                            1.4f);
        }
        if (focus_ring_) {
            g.setColour(colour::kAccent.withAlpha(.85f));
            g.drawRoundedRectangle(b.reduced(.75f), kind_ == Kind::kKneeTab ? 6.f : b.getHeight() * .5f, 1.5f);
        }
    }

    void DisplayButton::mouseUp(const juce::MouseEvent& e) {
        if (on_click && getLocalBounds().contains(e.getPosition())) {
            on_click();
        }
    }

    bool DisplayButton::keyPressed(const juce::KeyPress& key) {
        if (key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey) {
            if (on_click) {
                on_click();
            }
            return true;
        }
        return false;
    }

    void DisplayButton::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        repaint();
    }

    void DisplayButton::focusLost(FocusChangeType) {
        focus_ring_ = false;
        repaint();
    }

    std::unique_ptr<juce::AccessibilityHandler> DisplayButton::createAccessibilityHandler() {
        class Handler final : public juce::AccessibilityHandler {
        public:
            explicit Handler(DisplayButton& b) :
                AccessibilityHandler(b, juce::AccessibilityRole::toggleButton,
                                     juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press,
                                                                            [&b] {
                                                                                if (b.on_click) {
                                                                                    b.on_click();
                                                                                }
                                                                            })),
                button_(b) {
            }

            juce::AccessibleState getCurrentState() const override {
                auto s = AccessibilityHandler::getCurrentState().withCheckable();
                return button_.open_ ? s.withChecked() : s;
            }

        private:
            DisplayButton& button_;
        };
        return std::make_unique<Handler>(*this);
    }
}
