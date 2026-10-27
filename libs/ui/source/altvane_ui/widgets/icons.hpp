// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace altvane::ui::icons {
    enum class Icon {
        kPower, kUndo, kRedo, kSettings, kChevronLeft, kChevronRight, kChevronDown,
        kHeadphones, kTrash, kDynamic, kPlus, kClose, kSideChain, kFreeze, kCopy, kSearch, kStar, kFolder, kCurve,
        kFader, kTarget
    };

    /**
     * outline icons on a 16x16 grid, meant to be stroked at ~1.4 px
     */
    juce::Path get(Icon icon);

    void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float stroke = 1.4f);
}
