// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cmath>
#include <optional>

#include <altvane_ui/theme/fonts.hpp>
#include <altvane_ui/util/value_format.hpp>

#include "../PluginProcessor.hpp"
#include "../engine/loudness_targets.hpp"
#include "theme/limiter_tokens.hpp"

namespace altvane::limiter {
    namespace ui = altvane::ui;

    inline float value(juce::AudioProcessorValueTreeState& tree, const char* id) {
        return tree.getRawParameterValue(id)->load(std::memory_order::relaxed);
    }

    inline int index(juce::AudioProcessorValueTreeState& tree, const char* id) {
        return static_cast<int>(std::lround(value(tree, id)));
    }

    inline juce::Font captionFont() {
        return ui::Fonts::get().font(9.5f, ui::Fonts::kSemiBold).withExtraKerningFactor(.08f);
    }

    /**
     * "−12.3", "+0.4", "−∞" below the floor; levels read better without a forced plus sign
     */
    inline juce::String level(const float db, const int decimals = 1, const bool force_sign = false,
                              const float floor = -99.f) {
        if (!std::isfinite(db) || db <= floor) {
            return ui::format::kMinus + juce::String::charToString(static_cast<juce::juce_wchar>(0x221E));
        }
        const auto rounded = std::round(db * std::pow(10.f, static_cast<float>(decimals))) /
                             std::pow(10.f, static_cast<float>(decimals));
        auto text = juce::String(std::abs(rounded), decimals);
        if (rounded < 0.f) {
            return ui::format::kMinus + text;
        }
        return force_sign && rounded > 0.f ? "+" + text : text;
    }

    /**
     * the delivery target the session asks for, with the custom values filled in
     */
    inline LoudnessTarget currentTarget(juce::AudioProcessorValueTreeState& na) {
        const auto i = static_cast<size_t>(std::clamp(index(na, zlstate::PLoudnessTarget::kID), 0,
                                                      static_cast<int>(kLoudnessTargets.size()) - 1));
        auto target = kLoudnessTargets[i];
        if (i == kCustomTarget) {
            target.lufs = value(na, zlstate::PCustomTargetLUFS::kID);
            target.true_peak = value(na, zlstate::PCustomTargetTruePeak::kID);
        }
        return target;
    }

    /**
     * how far integrated loudness is off its target: green within 1 LU, amber within 2, red beyond
     */
    inline juce::Colour targetColour(const float lu_off) {
        const auto off = std::abs(lu_off);
        return off <= 1.f ? tokens::colour::kOnTarget : off <= 2.f ? tokens::colour::kNearTarget
                                                                   : tokens::colour::kOffTarget;
    }

    /**
     * a floating card: soft shadow, raised glass fill, hairline edge
     */
    inline void paintCard(juce::Graphics& g, const juce::Rectangle<float> card, const float radius) {
        for (int i = 3; i >= 1; --i) {
            const auto spread = static_cast<float>(i) * 6.f;
            g.setColour(juce::Colours::black.withAlpha(.09f));
            g.fillRoundedRectangle(card.expanded(spread * .5f).translated(0.f, spread), radius + spread * .5f);
        }
        g.setColour(juce::Colour{0xD615181C});
        g.fillRoundedRectangle(card, radius);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(.035f), card.getX(), card.getY(),
                                               juce::Colours::transparentWhite, card.getX(), card.getY() + 40.f,
                                               false));
        g.fillRoundedRectangle(card, radius);
        g.setColour(tokens::colour::kHairlineStrong);
        g.drawRoundedRectangle(card.reduced(.5f), radius, 1.f);
    }
}
