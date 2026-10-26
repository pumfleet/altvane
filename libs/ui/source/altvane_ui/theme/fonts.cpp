// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "fonts.hpp"

#include "AltvaneUIBinary.h"

namespace altvane::ui {
    Fonts::Fonts() {
        regular_ = juce::Typeface::createSystemTypefaceFor(AltvaneUIBinary::InterRegular_ttf,
                                                           AltvaneUIBinary::InterRegular_ttfSize);
        semi_bold_ = juce::Typeface::createSystemTypefaceFor(AltvaneUIBinary::InterSemiBold_ttf,
                                                             AltvaneUIBinary::InterSemiBold_ttfSize);
    }

    Fonts& Fonts::get() {
        static Fonts instance;
        return instance;
    }

    juce::Font Fonts::font(const float height, const Weight weight) const {
        return juce::Font(juce::FontOptions(getTypeface(weight)).withHeight(height));
    }

    float Fonts::getTabularWidth(const juce::String& text, const float height, const Weight weight) const {
        const auto f = font(height, weight);
        const auto digit_w = juce::GlyphArrangement::getStringWidth(f, "0");
        float w = 0.f;
        for (const auto c : text) {
            if (juce::CharacterFunctions::isDigit(c)) {
                w += digit_w;
            } else {
                w += juce::GlyphArrangement::getStringWidth(f, juce::String::charToString(c));
            }
        }
        return w;
    }

    void Fonts::drawTabular(juce::Graphics& g, const juce::String& text, const juce::Rectangle<float> area,
                            const juce::Justification justification, const float height,
                            const Weight weight) const {
        const auto f = font(height, weight);
        g.setFont(f);
        const auto digit_w = juce::GlyphArrangement::getStringWidth(f, "0");
        const auto total_w = getTabularWidth(text, height, weight);
        auto x = area.getX();
        if (justification.testFlags(juce::Justification::horizontallyCentred)) {
            x = area.getCentreX() - total_w * .5f;
        } else if (justification.testFlags(juce::Justification::right)) {
            x = area.getRight() - total_w;
        }
        const auto baseline = area.getCentreY() + (f.getAscent() - f.getDescent()) * .5f;
        juce::GlyphArrangement glyphs;
        for (const auto c : text) {
            const auto s = juce::String::charToString(c);
            const auto w = juce::GlyphArrangement::getStringWidth(f, s);
            const auto slot = juce::CharacterFunctions::isDigit(c) ? digit_w : w;
            glyphs.addLineOfText(f, s, x + (slot - w) * .5f, baseline);
            x += slot;
        }
        glyphs.draw(g);
    }
}
