// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace altvane::compressor::graph {
    /**
     * a small hover card: a title row, then label / value rows
     * placed to the right of the anchor, or to its left when there is no room
     */
    void drawReadout(juce::Graphics& g, juce::Point<float> anchor, const std::vector<std::pair<juce::String, juce::String>>& rows,
                     juce::Rectangle<float> bounds, float width = 132.f);

    /**
     * dB values on grids and meters, with a typographic minus and no unit
     */
    juce::String gridDB(float db);

    /**
     * grid spacing for a dB range
     */
    float gridStep(float range_db);

    juce::Font labelFont(float height = 10.f);
}
