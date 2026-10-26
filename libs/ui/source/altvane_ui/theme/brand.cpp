// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "brand.hpp"

#include "fonts.hpp"
#include "tokens.hpp"

namespace altvane::ui::brand {
    float drawLockup(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& product) {
        const auto mark_area = area.removeFromLeft(16.f).withSizeKeepingCentre(16.f, 16.f);
        drawMark(g, mark_area, tokens::colour::kAccent, 1.4f);
        area.removeFromLeft(10.f);

        const auto height = tokens::type::kTitle - 2.f;
        const auto name_font = Fonts::get().font(height, Fonts::kSemiBold).withExtraKerningFactor(.16f);
        const auto name = juce::String(kName).toUpperCase();
        const auto name_width = juce::GlyphArrangement::getStringWidth(name_font, name);
        g.setColour(tokens::colour::kTextPrimary);
        g.setFont(name_font);
        g.drawText(name, area.removeFromLeft(name_width + 2.f), juce::Justification::centredLeft);

        if (product.isEmpty()) {
            return area.getX();
        }
        area.removeFromLeft(7.f);
        const auto product_font = Fonts::get().font(height).withExtraKerningFactor(.08f);
        const auto product_width = juce::GlyphArrangement::getStringWidth(product_font, product);
        g.setColour(tokens::colour::kTextSecondary);
        g.setFont(product_font);
        g.drawText(product, area.removeFromLeft(product_width + 2.f), juce::Justification::centredLeft);
        return area.getX();
    }
}
