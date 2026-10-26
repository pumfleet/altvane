// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_graphics/juce_graphics.h>

/**
 * the Altvane brand: name, mark and the header lockup every product shows
 * the mark is a placeholder (a weather vane) until final logo art exists;
 * mark() is header-only so tools without the full UI library (tools/icons) draw the same shape
 */
namespace altvane::ui::brand {
    inline constexpr auto kName = "Altvane";

    struct Mark {
        juce::Path strokes; // stroke at ~1/12 of the mark's size
        juce::Path fills;
    };

    /**
     * the mark on a unit square (y = 0 is the top): a weather vane pointing up and to the right, with a solid
     * head, a notched tail plate and the pivot it turns on
     */
    inline Mark mark() {
        // drawn pointing right along y = .5, then turned to point up-right
        Mark m;
        m.strokes.startNewSubPath(.10f, .5f);
        m.strokes.lineTo(.78f, .5f);
        m.fills.addTriangle(.96f, .5f, .72f, .36f, .72f, .64f);
        // the tail plate, notched like fletching
        m.fills.startNewSubPath(.02f, .36f);
        m.fills.lineTo(.24f, .36f);
        m.fills.lineTo(.32f, .5f);
        m.fills.lineTo(.24f, .64f);
        m.fills.lineTo(.02f, .64f);
        m.fills.lineTo(.10f, .5f);
        m.fills.closeSubPath();
        m.fills.addEllipse(juce::Rectangle<float>(.13f, .13f).withCentre({.5f, .5f}));
        const auto turn = juce::AffineTransform::rotation(-juce::MathConstants<float>::pi * .25f, .5f, .5f)
            .scaled(1.2f, 1.2f, .5f, .5f);
        m.strokes.applyTransform(turn);
        m.fills.applyTransform(turn);
        return m;
    }

    inline void drawMark(juce::Graphics& g, const juce::Rectangle<float> area, const juce::Colour colour,
                         const float stroke) {
        const auto side = std::min(area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
        const auto transform = juce::AffineTransform::scale(side).translated(box.getX(), box.getY());
        auto [strokes, fills] = mark();
        strokes.applyTransform(transform);
        fills.applyTransform(transform);
        g.setColour(colour);
        g.strokePath(strokes, juce::PathStrokeType(stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        g.fillPath(fills);
    }

    /**
     * mark, "ALTVANE" and the product's short name (e.g. "EQ"), left-aligned and vertically centred in area
     * returns the x where the lockup ends
     */
    float drawLockup(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& product);
}
