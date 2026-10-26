// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/theme/tokens.hpp>

#include "../../dsp/stems.hpp"

/**
 * Altvane Dialogue's tokens, on top of the shared design system
 * every shared token is reachable through these namespaces too, e.g. tokens::colour::kTextPrimary
 */
namespace altvane::dialogue::tokens {
    using namespace altvane::ui::tokens;

    namespace colour {
        using namespace altvane::ui::tokens::colour;

        // stems: the voice is the light, the room cool, its echo between; kept apart from the accent choices,
        // which stay for interaction
        inline const std::array<juce::Colour, dsp::kNumStems> kStems{
            juce::Colour{0xFFF2E3C6}, // voice: warm ivory
            juce::Colour{0xFF6E9CC6}, // ambience: steel blue
            juce::Colour{0xFFB592DE}, // reverb: mauve
        };

        inline juce::Colour stem(const size_t s) {
            return kStems[s];
        }
    }

    namespace size {
        using namespace altvane::ui::tokens::size;

        // control deck
        inline constexpr float kDeckHeight = 214.f;
        inline constexpr float kCardWidth = 168.f;

        // visualiser
        inline constexpr float kNodeRadius = 6.f;
        inline constexpr float kNodeHitRadius = 14.f;

        // window, in logical pixels
        inline constexpr int kDefaultWidth = 900;
        inline constexpr int kDefaultHeight = 580;
        inline constexpr int kMinWidth = 680;
        inline constexpr int kMinHeight = 460;
        inline constexpr int kMaxWidth = 2400;
        inline constexpr int kMaxHeight = 1600;
    }
}
