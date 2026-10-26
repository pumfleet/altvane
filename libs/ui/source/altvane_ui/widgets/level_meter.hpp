// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <juce_graphics/juce_graphics.h>

namespace altvane::ui {
    /**
     * stereo peak meter ballistics and drawing, +6 .. -60 dB, with a numeric peak hold
     * the owner feeds new peaks every frame and paints it into its own areas
     */
    class LevelMeter final {
    public:
        /**
         * @return whether the meter needs a repaint
         */
        bool tick(double dt, float peak_left_db, float peak_right_db);

        void resetPeak() { peak_hold_ = -120.f; }

        /**
         * @param meter the bar area
         * @param readout where the peak hold value is written
         */
        void paint(juce::Graphics& g, juce::Rectangle<float> meter, juce::Rectangle<float> readout) const;

        float getLevel(const size_t channel) const { return levels_[channel]; }

    private:
        std::array<float, 2> levels_{-120.f, -120.f};
        float peak_hold_{-120.f};
        double peak_age_{0.0};
    };
}
