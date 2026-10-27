// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <altvane_ui/theme/tokens.hpp>

/**
 * Altvane Limiter's tokens, on top of the shared design system
 * every shared token is reachable through these namespaces too, e.g. tokens::colour::kTextPrimary
 */
namespace altvane::limiter::tokens {
    using namespace altvane::ui::tokens;

    namespace colour {
        using namespace altvane::ui::tokens::colour;

        // the signal before the limiter: the analyzer's cool grey, as a soft fill
        inline const juce::Colour kInput = kSpectrumPost;
        // the signal after it: near white, so the gap to the input is what the limiter took
        inline const juce::Colour kOutput{0xFFECF1F6};
        // and its body: a cool steel that sits between the analyzer grey and the accent
        inline const juce::Colour kOutputBody{0xFF6F97B4};
        // gain reduction hangs from the top in the danger red
        inline const juce::Colour kReduction = kDanger;
        // loudness: warm gold, apart from the (selectable) accent, which marks what can be dragged
        inline const juce::Colour kLoudness{0xFFE8C98A};
        // within a target / near it / off it
        inline const juce::Colour kOnTarget{0xFF5FD39A};
        inline const juce::Colour kNearTarget = kWarning;
        inline const juce::Colour kOffTarget = kDanger;
        // meter bars
        inline const juce::Colour kMeterTop{0xF296E0EC};
        inline const juce::Colour kMeterBottom{0xBF478CA0};
    }

    namespace size {
        using namespace altvane::ui::tokens::size;

        inline constexpr float kRailWidth = 164.f;
        inline constexpr float kCardHeight = 118.f;
        inline constexpr float kCardRadius = 12.f;
        inline constexpr float kLoudnessWidth = 244.f;
        inline constexpr float kPanelTabWidth = 18.f;
        inline constexpr float kPanelTabHeight = 74.f;

        // window, in logical pixels
        inline constexpr int kDefaultWidth = 1120;
        inline constexpr int kDefaultHeight = 700;
        inline constexpr int kMinWidth = 960;
        inline constexpr int kMinHeight = 560;
        inline constexpr int kMaxWidth = 2400;
        inline constexpr int kMaxHeight = 1600;
    }
}
