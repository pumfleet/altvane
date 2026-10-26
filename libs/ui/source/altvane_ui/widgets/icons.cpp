// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "icons.hpp"

#include <numbers>

namespace altvane::ui::icons {
    juce::Path get(const Icon icon) {
        juce::Path p;
        switch (icon) {
        case Icon::kPower:
            p.addCentredArc(8.f, 8.8f, 5.6f, 5.6f, 0.f, juce::degreesToRadians(40.f),
                            juce::degreesToRadians(320.f), true);
            p.startNewSubPath(8.f, 2.2f);
            p.lineTo(8.f, 8.f);
            break;
        case Icon::kUndo:
            p.startNewSubPath(5.5f, 3.5f);
            p.lineTo(2.5f, 6.5f);
            p.lineTo(5.5f, 9.5f);
            p.startNewSubPath(2.5f, 6.5f);
            p.lineTo(10.f, 6.5f);
            p.cubicTo(12.5f, 6.5f, 14.f, 8.2f, 14.f, 10.2f);
            p.cubicTo(14.f, 12.2f, 12.5f, 13.5f, 10.f, 13.5f);
            p.lineTo(7.f, 13.5f);
            break;
        case Icon::kRedo:
            p.startNewSubPath(10.5f, 3.5f);
            p.lineTo(13.5f, 6.5f);
            p.lineTo(10.5f, 9.5f);
            p.startNewSubPath(13.5f, 6.5f);
            p.lineTo(6.f, 6.5f);
            p.cubicTo(3.5f, 6.5f, 2.f, 8.2f, 2.f, 10.2f);
            p.cubicTo(2.f, 12.2f, 3.5f, 13.5f, 6.f, 13.5f);
            p.lineTo(9.f, 13.5f);
            break;
        case Icon::kSettings:
            // three sliders, reads as "settings" without a busy cog
            p.startNewSubPath(2.5f, 4.5f);
            p.lineTo(13.5f, 4.5f);
            p.startNewSubPath(2.5f, 8.f);
            p.lineTo(13.5f, 8.f);
            p.startNewSubPath(2.5f, 11.5f);
            p.lineTo(13.5f, 11.5f);
            p.addEllipse(9.f, 3.f, 3.f, 3.f);
            p.addEllipse(4.f, 6.5f, 3.f, 3.f);
            p.addEllipse(7.5f, 10.f, 3.f, 3.f);
            break;
        case Icon::kChevronLeft:
            p.startNewSubPath(10.f, 3.5f);
            p.lineTo(5.5f, 8.f);
            p.lineTo(10.f, 12.5f);
            break;
        case Icon::kChevronRight:
            p.startNewSubPath(6.f, 3.5f);
            p.lineTo(10.5f, 8.f);
            p.lineTo(6.f, 12.5f);
            break;
        case Icon::kChevronDown:
            p.startNewSubPath(4.f, 6.5f);
            p.lineTo(8.f, 10.f);
            p.lineTo(12.f, 6.5f);
            break;
        case Icon::kHeadphones:
            p.startNewSubPath(2.5f, 11.f);
            p.lineTo(2.5f, 8.5f);
            p.cubicTo(2.5f, 5.f, 5.f, 2.5f, 8.f, 2.5f);
            p.cubicTo(11.f, 2.5f, 13.5f, 5.f, 13.5f, 8.5f);
            p.lineTo(13.5f, 11.f);
            p.addRoundedRectangle(2.f, 9.5f, 3.f, 4.5f, 1.f);
            p.addRoundedRectangle(11.f, 9.5f, 3.f, 4.5f, 1.f);
            break;
        case Icon::kTrash:
            p.startNewSubPath(2.5f, 4.5f);
            p.lineTo(13.5f, 4.5f);
            p.startNewSubPath(6.f, 4.5f);
            p.lineTo(6.f, 2.5f);
            p.lineTo(10.f, 2.5f);
            p.lineTo(10.f, 4.5f);
            p.startNewSubPath(4.f, 4.5f);
            p.lineTo(4.8f, 13.5f);
            p.lineTo(11.2f, 13.5f);
            p.lineTo(12.f, 4.5f);
            break;
        case Icon::kDynamic:
            // a curve with a range arrow
            p.startNewSubPath(1.5f, 11.f);
            p.cubicTo(5.f, 11.f, 6.f, 5.f, 8.f, 5.f);
            p.cubicTo(10.f, 5.f, 11.f, 11.f, 14.5f, 11.f);
            p.startNewSubPath(8.f, 7.5f);
            p.lineTo(8.f, 13.5f);
            p.startNewSubPath(6.5f, 12.f);
            p.lineTo(8.f, 13.5f);
            p.lineTo(9.5f, 12.f);
            break;
        case Icon::kPlus:
            p.startNewSubPath(8.f, 3.f);
            p.lineTo(8.f, 13.f);
            p.startNewSubPath(3.f, 8.f);
            p.lineTo(13.f, 8.f);
            break;
        case Icon::kClose:
            p.startNewSubPath(4.f, 4.f);
            p.lineTo(12.f, 12.f);
            p.startNewSubPath(12.f, 4.f);
            p.lineTo(4.f, 12.f);
            break;
        case Icon::kSideChain:
            p.startNewSubPath(1.5f, 8.f);
            p.lineTo(5.5f, 8.f);
            p.addEllipse(5.5f, 5.f, 6.f, 6.f);
            p.startNewSubPath(11.5f, 8.f);
            p.lineTo(14.5f, 8.f);
            p.startNewSubPath(8.5f, 14.5f);
            p.lineTo(8.5f, 11.f);
            break;
        case Icon::kCopy:
            // arrow to the other slot
            p.addRoundedRectangle(2.5f, 5.f, 7.f, 8.5f, 1.5f);
            p.startNewSubPath(6.f, 2.5f);
            p.lineTo(12.f, 2.5f);
            p.quadraticTo(13.5f, 2.5f, 13.5f, 4.f);
            p.lineTo(13.5f, 10.5f);
            break;
        case Icon::kSearch:
            p.addEllipse(2.5f, 2.5f, 8.f, 8.f);
            p.startNewSubPath(9.5f, 9.5f);
            p.lineTo(13.5f, 13.5f);
            break;
        case Icon::kStar:
            for (int i = 0; i < 10; ++i) {
                const auto a = static_cast<float>(i) * std::numbers::pi_v<float> / 5.f - std::numbers::pi_v<float> / 2.f;
                const auto r = i % 2 == 0 ? 6.f : 2.6f;
                const auto x = 8.f + r * std::cos(a), y = 8.4f + r * std::sin(a);
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            p.closeSubPath();
            break;
        case Icon::kFolder:
            p.startNewSubPath(2.f, 4.f);
            p.lineTo(6.5f, 4.f);
            p.lineTo(8.f, 5.5f);
            p.lineTo(14.f, 5.5f);
            p.lineTo(14.f, 13.f);
            p.lineTo(2.f, 13.f);
            p.closeSubPath();
            break;
        case Icon::kFreeze:
            for (int i = 0; i < 3; ++i) {
                const auto a = static_cast<float>(i) * std::numbers::pi_v<float> / 3.f;
                p.startNewSubPath(8.f + 6.f * std::cos(a), 8.f + 6.f * std::sin(a));
                p.lineTo(8.f - 6.f * std::cos(a), 8.f - 6.f * std::sin(a));
            }
            break;
        }
        return p;
    }

    void draw(juce::Graphics& g, const Icon icon, const juce::Rectangle<float> area, const juce::Colour colour,
              const float stroke) {
        const auto side = std::min(area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float>(side, side).withCentre(area.getCentre());
        const auto scale = side / 16.f;
        auto p = get(icon);
        p.applyTransform(juce::AffineTransform::scale(scale).translated(box.getX(), box.getY()));
        g.setColour(colour);
        g.strokePath(p, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
