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
#include "../analysis/side_spectrum.hpp"
#include "../graph/side_eq_view.hpp"

namespace altvane::compressor {
    /**
     * the side-chain drawer: what the detector listens to (internal or external, with a listen switch),
     * how it handles stereo, the side-chain gain and per-channel amounts, and the side-chain EQ
     */
    class SideChainPanel final : public juce::Component {
    public:
        SideChainPanel(CompBridge& bridge, SideSpectrum& spectrum);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseDown(const juce::MouseEvent& e) override;

        void mouseUp(const juce::MouseEvent& e) override;

    private:
        CompBridge& bridge_;
        ui::Chip internal_{"Internal"}, external_{"External"}, listen_{"Listen"}, swap_{"Swap"}, eq_on_{"EQ"};
        ui::Selector stereo_;
        ui::Knob gain_, link_, amount1_, amount2_;
        SideEqView eq_;
        // a long press on Listen only listens while held
        juce::uint32 listen_down_ms_{0};
        bool listen_was_on_{false}, listen_pressed_{false};
        float divider_x_{0.f};
    };
}
