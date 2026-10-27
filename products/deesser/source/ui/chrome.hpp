// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/widgets/level_meter.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "../PluginProcessor.hpp"

namespace altvane::deesser {
    /**
     * bottom bar, the settings made once per track: stereo mode and link, side chain, look-ahead (which is the
     * latency), output and bypass
     */
    class Footer final : public juce::Component {
    public:
        Footer(PluginProcessor& p, ui::ParamEditor& editor);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        ui::ValueField& getLookahead() { return lookahead_; }

    private:
        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        std::array<ui::Chip, 3> stereo_{ui::Chip{"Stereo"}, ui::Chip{"Mid"}, ui::Chip{"Side"}};
        std::array<ui::Chip, 2> side_chain_{ui::Chip{"Int"}, ui::Chip{"Ext"}};
        ui::ValueField link_, lookahead_, output_;
        ui::IconButton bypass_{ui::icons::Icon::kPower};
        juce::Rectangle<float> side_chain_label_;
        std::array<float, 2> separators_{};
        bool show_stereo_{true}, have_side_chain_{false};

        void setChoice(const char* id, int index, const juce::String& name);
    };

    /**
     * right rail: gain reduction (hanging from the top) and the output level
     */
    class MeterRail final : public juce::Component {
    public:
        explicit MeterRail(PluginProcessor& p);

        /**
         * @param reduction_db the largest gain reduction since the last frame
         */
        void tick(double dt, float reduction_db);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseUp(const juce::MouseEvent& e) override;

    private:
        static constexpr float kReductionScaleDb = 24.f;

        PluginProcessor& p_ref_;
        ui::LevelMeter output_;
        float reduction_{0.f}, reduction_hold_{0.f};
        double hold_age_{0.0};
        juce::Rectangle<float> gr_label_, gr_meter_, gr_readout_, out_label_, out_meter_, out_readout_;

        void paintReduction(juce::Graphics& g) const;
    };
}
