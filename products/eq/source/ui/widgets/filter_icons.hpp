// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "altvane_ui/widgets/icons.hpp"
#include "../../dsp/filter/filter_design/filter_design.hpp"

namespace altvane::eq::icons {
    // the shared outline icons, plus filter shapes
    using namespace altvane::ui::icons;

    /**
     * a miniature frequency response of a filter shape, computed from the real filter prototype
     * returned in a 0..1 box (y = 0 is the top)
     */
    const juce::Path& shape(zldsp::filter::FilterType type);

    void drawShape(juce::Graphics& g, zldsp::filter::FilterType type, juce::Rectangle<float> area,
                   juce::Colour colour, float stroke = 1.4f);
}
