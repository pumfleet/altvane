// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "slider.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"

namespace altvane::ui {
    using namespace tokens;

    namespace {
        constexpr float kRowHeight = 16.f;
        constexpr float kTrackHeight = 3.f;
        constexpr float kThumb = 9.f;
    }

    Slider::Slider() {
        drag_horizontal_ = true;
        drag_pixels_ = 200.f;
    }

    juce::Rectangle<float> Slider::getTrackArea() const {
        auto b = getLocalBounds().toFloat().reduced(kThumb * .5f, 0.f);
        b.removeFromTop(kRowHeight + 6.f);
        return b.removeFromTop(kTrackHeight);
    }

    juce::Rectangle<int> Slider::getTextArea() const {
        auto b = getLocalBounds();
        return b.removeFromTop(static_cast<int>(kRowHeight)).removeFromRight(b.getWidth() / 2);
    }

    void Slider::paint(juce::Graphics& g) {
        const auto alpha = enabled_look_ ? 1.f : .35f;
        auto row = getLocalBounds().toFloat().removeFromTop(kRowHeight);

        g.setFont(Fonts::get().font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f));
        g.setColour(colour::kTextTertiary.withMultipliedAlpha(alpha));
        g.drawText(label_.toUpperCase(), row, juce::Justification::centredLeft);
        g.setColour((dragging_ ? colour::kTextPrimary : colour::kTextPrimary.withMultipliedAlpha(.82f))
                    .withMultipliedAlpha(alpha));
        Fonts::get().drawTabular(g, format(value_), row, juce::Justification::centredRight, type::kBody);

        const auto track = getTrackArea();
        g.setColour(colour::kHairlineStrong.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(track, kTrackHeight * .5f);

        const auto x_of = [&](const float norm) { return track.getX() + norm * track.getWidth(); };
        const auto x = x_of(getNormalised());
        const auto from = x_of(bipolar_ ? range_.convertTo0to1(default_) : 0.f);
        const auto accent = (accent_.isTransparent() ? colour::kAccent : accent_).withMultipliedAlpha(alpha);
        g.setColour(accent);
        g.fillRoundedRectangle(juce::Rectangle<float>(std::min(x, from), track.getY(), std::abs(x - from),
                                                      kTrackHeight), kTrackHeight * .5f);

        if (hovered_ || dragging_ || focus_ring_) {
            const auto thumb = juce::Rectangle<float>(kThumb, kThumb).withCentre({x, track.getCentreY()});
            g.setColour(colour::kWindow.withAlpha(.8f));
            g.fillEllipse(thumb.expanded(3.f));
            g.setColour(colour::kTextPrimary);
            g.fillEllipse(thumb);
        }
        paintFocusRing(g, getLocalBounds().toFloat(), size::kRadiusSmall);
    }
}
