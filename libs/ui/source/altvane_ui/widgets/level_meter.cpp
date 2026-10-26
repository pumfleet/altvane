// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "level_meter.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"

namespace altvane::ui {
    using namespace tokens;

    bool LevelMeter::tick(const double dt, const float peak_left_db, const float peak_right_db) {
        bool changed = false;
        const std::array peaks{peak_left_db, peak_right_db};
        for (size_t c = 0; c < 2; ++c) {
            const auto peak = peaks[c];
            const auto decayed = levels_[c] - static_cast<float>(dt) * 24.f;
            const auto next = std::max(peak, decayed);
            changed = changed || std::abs(next - levels_[c]) > .05f;
            levels_[c] = std::max(next, -120.f);
            if (peak >= peak_hold_) {
                peak_hold_ = peak;
                peak_age_ = 0.0;
            }
        }
        peak_age_ += dt;
        if (peak_age_ > 2.0) {
            peak_hold_ = std::max(levels_[0], levels_[1]);
            peak_age_ = 1.5;
            changed = true;
        }
        return changed;
    }

    void LevelMeter::paint(juce::Graphics& g, const juce::Rectangle<float> meter, const juce::Rectangle<float> readout) const {
        auto to_y = [meter](const float db) {
            const auto t = std::clamp((6.f - db) / 66.f, 0.f, 1.f);
            return meter.getY() + t * meter.getHeight();
        };
        const auto bar_w = (meter.getWidth() - 2.f) * .5f;
        for (size_t c = 0; c < 2; ++c) {
            auto bar = juce::Rectangle<float>(meter.getX() + static_cast<float>(c) * (bar_w + 2.f),
                                              meter.getY(), bar_w, meter.getHeight());
            g.setColour(colour::kControl);
            g.fillRoundedRectangle(bar, 1.5f);
            const auto top = to_y(levels_[c]);
            if (top < bar.getBottom()) {
                auto fill = bar.withTop(top);
                g.setGradientFill(juce::ColourGradient(colour::kDanger, 0.f, to_y(3.f),
                                                       colour::kAccent.withAlpha(.75f), 0.f, to_y(-24.f), false));
                g.fillRoundedRectangle(fill, 1.5f);
            }
        }
        // 0 dB reference across both bars
        g.setColour(colour::kGraph);
        g.fillRect(juce::Rectangle<float>(meter.getX(), to_y(0.f), meter.getWidth(), 1.f));

        g.setColour(peak_hold_ > 0.f ? colour::kDanger : colour::kTextSecondary);
        Fonts::get().drawTabular(g, peak_hold_ < -99.f ? juce::String(juce::CharPointer_UTF8("\xe2\x80\x93"))
                                                       : juce::String(peak_hold_, 1),
                                 readout, juce::Justification::centred, type::kCaption);
    }
}
