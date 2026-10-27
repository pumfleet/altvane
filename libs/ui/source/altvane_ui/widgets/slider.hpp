// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "widgets.hpp"

namespace altvane::ui {
    /**
     * a horizontal slider for secondary values: caption and value on one row, a thin track below,
     * a thumb that shows on hover; dragged sideways (shift = fine), otherwise behaves like every ValueControl
     */
    class Slider final : public ValueControl {
    public:
        Slider();

        /**
         * fill from the default value instead of the start of the range
         */
        void setBipolar(const bool bipolar) { bipolar_ = bipolar; repaint(); }

        void paint(juce::Graphics& g) override;

    protected:
        juce::Rectangle<int> getTextArea() const override;

    private:
        bool bipolar_{false};

        juce::Rectangle<float> getTrackArea() const;
    };
}
