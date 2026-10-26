// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace contour::ui {
    /**
     * typefaces and text helpers
     * Inter Regular / SemiBold are embedded; numerals are laid out in fixed-width slots
     * so values do not jitter while being dragged
     */
    class Fonts {
    public:
        enum Weight { kRegular, kSemiBold };

        static Fonts& get();

        juce::Font font(float height, Weight weight = kRegular) const;

        /**
         * draw text with tabular digits
         */
        void drawTabular(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                         juce::Justification justification, float height, Weight weight = kRegular) const;

        float getTabularWidth(const juce::String& text, float height, Weight weight = kRegular) const;

        juce::Typeface::Ptr getTypeface(Weight weight) const {
            return weight == kSemiBold ? semi_bold_ : regular_;
        }

    private:
        Fonts();

        juce::Typeface::Ptr regular_, semi_bold_;
    };
}
