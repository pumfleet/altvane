// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "altvane_ui/theme/tokens.hpp"

/**
 * Altvane Compressor's additions to the shared design tokens (see docs/design/mock.html)
 */
namespace altvane::ui::tokens {
    namespace colour {
        // gain reduction hangs from the top in red; upward gain (inflate, shape) is green
        inline const juce::Colour kReduction{0xFFF0656A};
        inline const juce::Colour kUpward{0xFF5FD39A};
        // the level history: input as a filled area, output as a light line
        inline const juce::Colour kInputFill{0xFF8FA3B8};
        inline const juce::Colour kOutputLine{0xFFECF1F6};
        // meter bars, top and bottom of their gradient
        inline const juce::Colour kMeterTop{0xF296E0EC};
        inline const juce::Colour kMeterBottom{0xBF478CA0};
        // floating glass surfaces over the display
        inline const juce::Colour kGlass{0xD615181C};
        // the eight side-chain EQ bands
        inline const std::array<juce::Colour, 8> kBands{
            juce::Colour{0xFF76B4FF}, juce::Colour{0xFFED9C55}, juce::Colour{0xFF6FC884}, juce::Colour{0xFFDF92D8},
            juce::Colour{0xFFC4B345}, juce::Colour{0xFF00C9D1}, juce::Colour{0xFFF98E8F}, juce::Colour{0xFFA1A7FF}
        };
    }

    namespace size {
        inline constexpr int kDefaultWidth = 1040;
        inline constexpr int kDefaultHeight = 640;
        inline constexpr int kMinWidth = 960;
        inline constexpr int kMinHeight = 560;
        inline constexpr int kMaxWidth = 3200;
        inline constexpr int kMaxHeight = 2000;

        // the IN / GR / OUT meters on the right
        inline constexpr float kMeterRailWidth = 76.f;
        // the side-chain drawer below the display
        inline constexpr float kSideChainHeight = 214.f;
        // the control card floats this far above the bottom of the display
        inline constexpr float kCardBottom = 40.f;
        // knob cells in the card
        inline constexpr int kKnobWidth = 66;
        inline constexpr int kKnobHeight = 84;
        inline constexpr int kSliderWidth = 124;
        inline constexpr int kSliderHeight = 34;
    }
}
