// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "graph_helpers.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/theme/tokens.hpp"
#include "altvane_ui/util/value_format.hpp"

namespace altvane::compressor::graph {
    using namespace ui::tokens;
    using ui::Fonts;

    juce::Font labelFont(const float height) {
        return Fonts::get().font(height, Fonts::kRegular);
    }

    juce::String gridDB(const float db) {
        const auto v = juce::roundToInt(db);
        return v < 0 ? ui::format::kMinus + juce::String(-v) : juce::String(v);
    }

    float gridStep(const float range_db) {
        return range_db <= 20.f ? 3.f : range_db <= 60.f ? 6.f : 12.f;
    }

    void drawReadout(juce::Graphics& g, const juce::Point<float> anchor,
                     const std::vector<std::pair<juce::String, juce::String>>& rows,
                     const juce::Rectangle<float> bounds, const float width) {
        if (rows.empty()) {
            return;
        }
        constexpr auto kRow = 16.f;
        const auto h = 10.f + kRow * static_cast<float>(rows.size());
        auto x = anchor.x + 12.f;
        if (x + width > bounds.getRight() - 6.f) {
            x = anchor.x - 12.f - width;
        }
        const auto y = std::clamp(anchor.y - h * .5f, bounds.getY() + 4.f, std::max(bounds.getBottom() - h - 4.f,
                                                                                    bounds.getY() + 4.f));
        const auto card = juce::Rectangle<float>(x, y, width, h);
        g.setColour(juce::Colours::black.withAlpha(.3f));
        g.fillRoundedRectangle(card.translated(0.f, 3.f).expanded(1.f), size::kRadius);
        g.setColour(colour::kOverlay);
        g.fillRoundedRectangle(card, size::kRadius);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(card.reduced(.5f), size::kRadius, 1.f);

        auto row_area = card.reduced(9.f, 5.f);
        for (size_t i = 0; i < rows.size(); ++i) {
            auto row = row_area.removeFromTop(kRow);
            g.setFont(Fonts::get().font(type::kSmall, i == 0 ? Fonts::kSemiBold : Fonts::kRegular));
            g.setColour(i == 0 ? colour::kTextSecondary : colour::kTextTertiary.withAlpha(.75f));
            g.drawText(rows[i].first, row, juce::Justification::centredLeft);
            g.setColour(colour::kTextPrimary);
            Fonts::get().drawTabular(g, rows[i].second, row, juce::Justification::centredRight, type::kSmall);
        }
    }
}
