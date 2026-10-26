// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "look_and_feel.hpp"

#include "fonts.hpp"
#include "tokens.hpp"

namespace contour::ui {
    using namespace tokens;

    LookAndFeel::LookAndFeel() {
        setColour(juce::PopupMenu::backgroundColourId, colour::kOverlay);
        setColour(juce::PopupMenu::textColourId, colour::kTextPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, colour::kControlHover);
        setColour(juce::PopupMenu::highlightedTextColourId, colour::kTextPrimary);
        setColour(juce::TooltipWindow::backgroundColourId, colour::kOverlay);
        setColour(juce::TooltipWindow::textColourId, colour::kTextPrimary);
        setColour(juce::TextEditor::textColourId, colour::kTextPrimary);
        setColour(juce::TextEditor::highlightColourId, colour::kAccent.withAlpha(.35f));
        setColour(juce::TextEditor::highlightedTextColourId, colour::kTextPrimary);
        setColour(juce::CaretComponent::caretColourId, colour::kAccent);
        setColour(juce::TextEditor::backgroundColourId, colour::kControl);
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, colour::kAccent.withAlpha(.6f));
        setDefaultSansSerifTypeface(Fonts::get().getTypeface(Fonts::kRegular));
        refreshAccent();
    }

    void LookAndFeel::refreshAccent() {
        setColour(juce::TextEditor::highlightColourId, colour::kAccent.withAlpha(.35f));
        setColour(juce::CaretComponent::caretColourId, colour::kAccent);
        setColour(juce::TextEditor::focusedOutlineColourId, colour::kAccent.withAlpha(.6f));
    }

    juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& font) {
        return Fonts::get().getTypeface(font.isBold() ? Fonts::kSemiBold : Fonts::kRegular);
    }

    juce::Font LookAndFeel::getPopupMenuFont() {
        return Fonts::get().font(type::kBody);
    }

    void LookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g, const int width, const int height,
                                                         const juce::PopupMenu::Options&) {
        const auto bounds = juce::Rectangle<float>(0.f, 0.f, static_cast<float>(width), static_cast<float>(height));
        g.setColour(colour::kRaised);
        g.fillRoundedRectangle(bounds, size::kRadius);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(bounds.reduced(.5f), size::kRadius, 1.f);
    }

    void LookAndFeel::getIdealPopupMenuItemSizeWithOptions(const juce::String& text, const bool is_separator,
                                                           int, int& ideal_width, int& ideal_height,
                                                           const juce::PopupMenu::Options&) {
        if (is_separator) {
            ideal_width = 50;
            ideal_height = 9;
            return;
        }
        ideal_height = 24;
        ideal_width = juce::roundToInt(juce::GlyphArrangement::getStringWidth(getPopupMenuFont(), text)) + 56;
    }

    void LookAndFeel::drawPopupMenuSectionHeaderWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                            const juce::String& section_name,
                                                            const juce::PopupMenu::Options&) {
        g.setColour(colour::kTextTertiary);
        g.setFont(Fonts::get().font(type::kCaption, Fonts::kSemiBold));
        g.drawText(section_name.toUpperCase(), area.reduced(12, 0).withTrimmedTop(4),
                   juce::Justification::centredLeft);
    }

    void LookAndFeel::drawPopupMenuItemWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                   const bool is_highlighted, const juce::PopupMenu::Item& item,
                                                   const juce::PopupMenu::Options&) {
        if (item.isSeparator) {
            g.setColour(colour::kHairline);
            g.fillRect(area.toFloat().withSizeKeepingCentre(static_cast<float>(area.getWidth()) - 16.f, 1.f));
            return;
        }
        auto r = area.toFloat().reduced(4.f, 1.f);
        if (is_highlighted && item.isEnabled) {
            g.setColour(colour::kControlHover);
            g.fillRoundedRectangle(r, size::kRadiusSmall);
        }
        const auto text_colour = !item.isEnabled
            ? colour::kTextDisabled
            : (item.colour.isTransparent() ? colour::kTextPrimary : item.colour);
        // tick
        auto tick_area = r.removeFromLeft(22.f);
        if (item.isTicked) {
            g.setColour(colour::kAccent);
            g.fillEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre(tick_area.getCentre()));
        }
        if (item.subMenu != nullptr) {
            auto arrow = r.removeFromRight(16.f);
            juce::Path p;
            const auto c = arrow.getCentre();
            p.startNewSubPath(c.x - 2.f, c.y - 3.5f);
            p.lineTo(c.x + 1.5f, c.y);
            p.lineTo(c.x - 2.f, c.y + 3.5f);
            g.setColour(colour::kTextTertiary);
            g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (item.shortcutKeyDescription.isNotEmpty()) {
            g.setColour(colour::kTextTertiary);
            g.setFont(Fonts::get().font(type::kSmall));
            g.drawText(item.shortcutKeyDescription, r.removeFromRight(70.f).withTrimmedRight(6.f),
                       juce::Justification::centredRight);
        }
        g.setColour(text_colour);
        g.setFont(getPopupMenuFont());
        g.drawText(item.text, r, juce::Justification::centredLeft, true);
    }

    juce::Rectangle<int> LookAndFeel::getTooltipBounds(const juce::String& tip_text, const juce::Point<int> screen_pos,
                                                       const juce::Rectangle<int> parent_area) {
        const auto f = Fonts::get().font(type::kSmall);
        const auto w = juce::roundToInt(juce::GlyphArrangement::getStringWidth(f, tip_text)) + 18;
        const auto h = 24;
        return juce::Rectangle<int>(screen_pos.x > parent_area.getCentreX() ? screen_pos.x - (w + 8) : screen_pos.x + 12,
                                    screen_pos.y > parent_area.getCentreY() ? screen_pos.y - (h + 8) : screen_pos.y + 14,
                                    w, h).constrainedWithin(parent_area);
    }

    void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, const int width, const int height) {
        const auto bounds = juce::Rectangle<float>(0.f, 0.f, static_cast<float>(width), static_cast<float>(height));
        g.setColour(colour::kRaised);
        g.fillRoundedRectangle(bounds, size::kRadiusSmall);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(bounds.reduced(.5f), size::kRadiusSmall, 1.f);
        g.setColour(colour::kTextPrimary);
        g.setFont(Fonts::get().font(type::kSmall));
        g.drawText(text, bounds.reduced(9.f, 0.f), juce::Justification::centredLeft, true);
    }

    void LookAndFeel::fillTextEditorBackground(juce::Graphics& g, const int width, const int height,
                                               juce::TextEditor&) {
        g.setColour(colour::kControl);
        g.fillRoundedRectangle(0.f, 0.f, static_cast<float>(width), static_cast<float>(height), size::kRadiusSmall);
    }

    void LookAndFeel::drawTextEditorOutline(juce::Graphics& g, const int width, const int height,
                                            juce::TextEditor& editor) {
        if (editor.hasKeyboardFocus(true)) {
            g.setColour(colour::kAccent.withAlpha(.55f));
            g.drawRoundedRectangle(.5f, .5f, static_cast<float>(width) - 1.f, static_cast<float>(height) - 1.f,
                                   size::kRadiusSmall, 1.f);
        }
    }

    void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, const int x, const int y, const int width,
                                    const int height, const bool is_vertical, const int thumb_start,
                                    const int thumb_size, const bool is_mouse_over, const bool is_mouse_down) {
        auto thumb = is_vertical
                         ? juce::Rectangle<int>(x, thumb_start, width, thumb_size)
                         : juce::Rectangle<int>(thumb_start, y, thumb_size, height);
        thumb = thumb.reduced(is_vertical ? 3 : 2, is_vertical ? 2 : 3);
        g.setColour(juce::Colours::white.withAlpha(is_mouse_down ? .3f : (is_mouse_over ? .2f : .1f)));
        g.fillRoundedRectangle(thumb.toFloat(), static_cast<float>(std::min(thumb.getWidth(), thumb.getHeight())) * .5f);
    }

    juce::CaretComponent* LookAndFeel::createCaretComponent(juce::Component* key_focus_owner) {
        auto* caret = juce::LookAndFeel_V4::createCaretComponent(key_focus_owner);
        caret->setColour(juce::CaretComponent::caretColourId, colour::kAccent);
        return caret;
    }
}
