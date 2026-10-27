// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../analysis/level_history.hpp"
#include "../model/comp_bridge.hpp"

namespace altvane::compressor {
    /**
     * the right rail: input, gain change and output meters on the display's dB scale, with peak readouts on top
     * (click a readout or a meter to reset the peaks); a marker on the input meter shows the threshold
     */
    class MeterRail final : public juce::Component {
    public:
        explicit MeterRail(CompBridge& bridge);

        void tick(double dt, const LevelHistory::Meters& meters);

        void paint(juce::Graphics& g) override;

        void mouseUp(const juce::MouseEvent& e) override;

        void resetPeaks();

    private:
        CompBridge& bridge_;
        std::array<float, 2> input_{-120.f, -120.f}, output_{-120.f, -120.f};
        float gain_{0.f};
        float input_peak_{-120.f}, output_peak_{-120.f}, gain_peak_{0.f};
        float scale_db_{54.f};
    };
}
