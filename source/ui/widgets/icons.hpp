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

#include "../../dsp/filter/filter_design/filter_design.hpp"

namespace contour::ui::icons {
    enum class Icon {
        kPower, kUndo, kRedo, kSettings, kChevronLeft, kChevronRight, kChevronDown,
        kHeadphones, kTrash, kDynamic, kPlus, kClose, kSideChain, kFreeze, kCopy, kSearch, kStar, kFolder
    };

    /**
     * outline icons on a 16x16 grid, meant to be stroked at ~1.4 px
     */
    juce::Path get(Icon icon);

    void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float stroke = 1.4f);

    /**
     * a miniature frequency response of a filter shape, computed from the real filter prototype
     * returned in a 0..1 box (y = 0 is the top)
     */
    const juce::Path& shape(zldsp::filter::FilterType type);

    void drawShape(juce::Graphics& g, zldsp::filter::FilterType type, juce::Rectangle<float> area,
                   juce::Colour colour, float stroke = 1.4f);
}
