// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <juce_graphics/juce_graphics.h>

/**
 * the Contour design system: every colour, size and duration used by the UI lives here
 * sizes are in logical pixels; the editor applies a global interface scale on top
 */
namespace contour::ui::tokens {
    namespace colour {
        // surfaces, darkest to lightest
        inline const juce::Colour kWindow{0xFF0A0B0D};
        inline const juce::Colour kGraph{0xFF0E1013};
        inline const juce::Colour kRaised{0xFF15181C};
        inline const juce::Colour kOverlay{0xEB1A1E23};
        inline const juce::Colour kControl{0xFF20252B};
        inline const juce::Colour kControlHover{0xFF282E35};

        // hairlines and grid
        inline const juce::Colour kHairline = juce::Colour{0x12FFFFFF};
        inline const juce::Colour kHairlineStrong = juce::Colour{0x1FFFFFFF};
        inline const juce::Colour kGridMajor = juce::Colour{0x13FFFFFF};
        inline const juce::Colour kGridMinor = juce::Colour{0x08FFFFFF};
        inline const juce::Colour kGridZero = juce::Colour{0x24FFFFFF};

        // text
        inline const juce::Colour kTextPrimary = juce::Colour{0xEBFFFFFF};
        inline const juce::Colour kTextSecondary = juce::Colour{0x9EFFFFFF};
        inline const juce::Colour kTextTertiary = juce::Colour{0x61FFFFFF};
        inline const juce::Colour kTextDisabled = juce::Colour{0x38FFFFFF};

        // single accent, used sparingly for active state; user-selectable, so not const
        inline juce::Colour kAccent{0xFF47D2E8};

        inline const std::array<std::pair<const char*, juce::Colour>, 5> kAccentChoices{
            {
                {"Cyan", juce::Colour{0xFF47D2E8}}, {"Violet", juce::Colour{0xFFA99BFF}},
                {"Amber", juce::Colour{0xFFF2B84B}}, {"Green", juce::Colour{0xFF5FD39A}},
                {"Rose", juce::Colour{0xFFF28BA8}}
            }
        };
        inline const juce::Colour kWarning{0xFFF2B84B};
        inline const juce::Colour kDanger{0xFFF0656A};

        // analyzer
        inline const juce::Colour kSpectrumPost{0xFF8FA3B8};
        inline const juce::Colour kSpectrumPre{0xFF5A6675};
        inline const juce::Colour kSpectrumSide{0xFFE59A62};

        // composite response curve
        inline const juce::Colour kCurve{0xFFF4F6F8};

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
        // spacing scale (4 pt grid)
        inline constexpr float kS1 = 4.f;
        inline constexpr float kS2 = 8.f;
        inline constexpr float kS3 = 12.f;
        inline constexpr float kS4 = 16.f;
        inline constexpr float kS5 = 24.f;
        inline constexpr float kS6 = 32.f;

        // corner radii
        inline constexpr float kRadiusSmall = 4.f;
        inline constexpr float kRadius = 6.f;
        inline constexpr float kRadiusLarge = 10.f;

        // chrome
        inline constexpr float kHeaderHeight = 40.f;
        inline constexpr float kFooterHeight = 36.f;
        inline constexpr float kRailWidth = 44.f;
        inline constexpr float kControlHeight = 24.f;

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

    namespace type {
        inline constexpr float kCaption = 10.f;
        inline constexpr float kSmall = 11.f;
        inline constexpr float kBody = 12.f;
        inline constexpr float kValue = 13.f;
        inline constexpr float kTitle = 15.f;
    }

    namespace motion {
        inline constexpr float kFast = .09f;
        inline constexpr float kBase = .16f;
        inline constexpr float kSlow = .24f;
        inline constexpr float kHoverSpringHz = 7.f;
        inline constexpr float kCurveMorphHz = 9.f;
        inline constexpr float kReadoutDelay = .08f;
    }
}
