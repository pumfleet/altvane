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
     * the controls behind More: detection (peak / RMS blend), envelope shaping and the output clipper
     */
    class AdvancedPopover final : public juce::Component {
    public:
        explicit AdvancedPopover(CompBridge& bridge);

        static juce::Point<int> getIdealSize() { return {486, 158}; }

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        bool keyPressed(const juce::KeyPress& key) override;

        std::function<void()> on_close;

    private:
        CompBridge& bridge_;
        ui::Switch rms_;
        ui::Slider rms_mix_, rms_length_, rms_speed_, pump_, smooth_, curve_, floor_, clipper_;
        std::array<juce::Rectangle<float>, 3> headers_;
    };
}
