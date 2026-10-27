// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/theme/fonts.hpp>
#include <altvane_ui/theme/tokens.hpp>

/**
 * Altvane De-Esser's tokens, on top of the shared design system
 * every shared token is reachable through these namespaces too, e.g. tokens::colour::kTextPrimary
 */
namespace altvane::deesser::tokens {
    using namespace altvane::ui::tokens;

    namespace colour {
        using namespace altvane::ui::tokens::colour;

        // sibilance: a warm apricot, the colour of everything the de-esser hears and takes; kept apart from the
        // accent choices (interaction) and from the gain reduction's red
        inline const juce::Colour kSibilance{0xFFFFB27A};
        // gain reduction
        inline const juce::Colour kReduction{0xFFF0656A};
        // levels in the history: the full signal, and the detection band
        inline const juce::Colour kLevel{0xFF8E99A6};
        inline const juce::Colour kBand{0xFFB9C6D3};
    }

    namespace size {
        using namespace altvane::ui::tokens::size;

        // the lower half: band view on the left, control deck on the right
        inline constexpr int kLowerHeight = 226;
        inline constexpr int kDeckWidth = 444;
        // the meter rail: gain reduction and output side by side
        inline constexpr int kMeterRailWidth = 76;

        inline constexpr float kHandleHitWidth = 7.f;

        // window, in logical pixels
        inline constexpr int kDefaultWidth = 1000;
        inline constexpr int kDefaultHeight = 640;
        inline constexpr int kMinWidth = 800;
        inline constexpr int kMinHeight = 520;
        inline constexpr int kMaxWidth = 2400;
        inline constexpr int kMaxHeight = 1600;
    }

    inline juce::Font captionFont() {
        return ui::Fonts::get().font(9.5f, ui::Fonts::kSemiBold).withExtraKerningFactor(.08f);
    }
}
