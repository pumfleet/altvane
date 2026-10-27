// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "altvane_ui/widgets/widgets.hpp"
#include "../model/comp_bridge.hpp"

namespace altvane::compressor {
    /**
     * bottom bar: oversampling and delta; the display's time, scale and peak / RMS; latency and bypass
     */
    class Footer final : public juce::Component {
    public:
        explicit Footer(CompBridge& bridge);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        CompBridge& bridge_;
        ui::Selector oversample_, time_, scale_;
        ui::Chip delta_{"Delta"}, peak_{"Peak"}, rms_{"RMS"};
        ui::IconButton bypass_{ui::icons::Icon::kPower};
        juce::Rectangle<float> oversample_label_, display_label_, latency_area_;
        float separator_x_{0.f};
        juce::String latency_text_;
    };
}
