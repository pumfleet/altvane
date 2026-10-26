// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "altvane_ui/widgets/widgets.hpp"
#include "../model/engine_bridge.hpp"
#include "../analysis/spectrum_worker.hpp"
#include "altvane_ui/widgets/level_meter.hpp"

namespace altvane::eq {
    /**
     * bottom bar: processing mode, analyzer sources, global gain controls
     */
    class Footer final : public juce::Component {
    public:
        std::function<void()> on_toggle_match;
        std::function<bool()> is_matching;

        explicit Footer(EngineBridge& bridge);

        ~Footer() override;

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        EngineBridge& bridge_;
        Selector phase_;
        Chip pre_{"Pre"}, post_{"Post"}, side_spectrum_{"SC"};
        IconButton analyzer_menu_{icons::Icon::kChevronDown};
        Chip match_{"EQ Match"};
        Chip ext_side_{"Side-chain"}, auto_gain_{"Auto Gain"};
        ValueField scale_, output_;
        IconButton output_menu_{icons::Icon::kChevronDown};
        IconButton bypass_{icons::Icon::kPower};
        bool loudness_learning_{false};

        void showAnalyzerMenu();
        void showOutputMenu();
        void setLoudnessLearning(bool on);
    };

    /**
     * right rail: EQ display range and the output meter
     */
    class OutputRail final : public juce::Component {
    public:
        OutputRail(EngineBridge& bridge, SpectrumWorker& spectrum);

        void tick(double dt);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseUp(const juce::MouseEvent& e) override;

    private:
        EngineBridge& bridge_;
        SpectrumWorker& spectrum_;
        LevelMeter meter_;
        juce::Rectangle<float> range_area_, meter_area_, peak_area_;
    };
}
