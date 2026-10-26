// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "altvane_ui/theme/tokens.hpp"

/**
 * Altvane EQ's tokens, on top of the shared design system
 * every shared token is reachable through these namespaces too, e.g. tokens::colour::kTextPrimary
 */
namespace altvane::eq::tokens {
    using namespace altvane::ui::tokens;

    namespace colour {
        using namespace altvane::ui::tokens::colour;

        // band palette, OKLCH L=0.76 C=0.13, ordered so neighbouring bands contrast
        inline const std::array<juce::Colour, 12> kBands{
            juce::Colour{0xFF76B4FF}, juce::Colour{0xFFED9C55}, juce::Colour{0xFF6FC884},
            juce::Colour{0xFFDF92D8}, juce::Colour{0xFFC4B345}, juce::Colour{0xFF00C9D1},
            juce::Colour{0xFFF98E8F}, juce::Colour{0xFFA1A7FF}, juce::Colour{0xFF37CCAC},
            juce::Colour{0xFFA7BD58}, juce::Colour{0xFF43C0F3}, juce::Colour{0xFFBE9DF7}
        };

        inline juce::Colour band(const size_t idx) {
            return kBands[idx % kBands.size()];
        }
    }

    namespace size {
        using namespace altvane::ui::tokens::size;

        // graph
        inline constexpr float kNodeRadius = 7.f;
        inline constexpr float kNodeHitRadius = 12.f;
        inline constexpr float kQHandleRadius = 4.f;
        inline constexpr float kCurveStroke = 1.6f;
        inline constexpr float kBandStroke = 1.2f;
        inline constexpr float kGraphPadding = 14.f;

        // band panel
        inline constexpr float kPanelWidth = 560.f;
        inline constexpr float kPanelHeight = 112.f;
        inline constexpr float kPanelDynamicExtra = 76.f;

        // window, in logical pixels
        inline constexpr int kDefaultWidth = 1040;
        inline constexpr int kDefaultHeight = 620;
        inline constexpr int kMinWidth = 760;
        inline constexpr int kMinHeight = 460;
        inline constexpr int kMaxWidth = 3200;
        inline constexpr int kMaxHeight = 2000;
    }
}
