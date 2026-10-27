// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/theme/fonts.hpp>
#include <altvane_ui/theme/tokens.hpp>

/**
 * Altvane Leveler's tokens, on top of the shared design system
 * every shared token is reachable through these namespaces too, e.g. tokens::colour::kTextPrimary
 */
namespace altvane::leveler::tokens {
    using namespace altvane::ui::tokens;

    namespace colour {
        using namespace altvane::ui::tokens::colour;

        // levels: the input dim, the leveled output light, the music key apart from both
        inline const juce::Colour kInput = kSpectrumPre;
        inline const juce::Colour kOutput{0xFFE8ECF0};
        inline const juce::Colour kKey = kSpectrumSide;
        // the gain the rider applies, and the ride states
        inline const juce::Colour kVoiced{0xFF5FD39A};
        inline const juce::Colour kUnvoiced = kWarning;
        inline const juce::Colour kWrite = kDanger;
    }

    namespace size {
        using namespace altvane::ui::tokens::size;

        inline constexpr float kDeckHeight = 150.f;
        inline constexpr float kRiderWidth = 148.f;
        // the gain lane below the levels, and the voicing strip between them
        inline constexpr float kGainLaneShare = .26f;
        inline constexpr float kStripHeight = 4.f;
        inline constexpr float kLineHitDistance = 6.f;

        // window, in logical pixels
        inline constexpr int kDefaultWidth = 960;
        inline constexpr int kDefaultHeight = 620;
        inline constexpr int kMinWidth = 760;
        inline constexpr int kMinHeight = 520;
        inline constexpr int kMaxWidth = 2400;
        inline constexpr int kMaxHeight = 1600;
    }

    namespace motion {
        using namespace altvane::ui::tokens::motion;

        // seconds of history across the display
        inline constexpr double kHistorySeconds = 8.0;
    }

    /**
     * small caps captions in panels and the footer
     */
    inline juce::Font captionFont() {
        return ui::Fonts::get().font(9.5f, ui::Fonts::kSemiBold).withExtraKerningFactor(.08f);
    }
}
