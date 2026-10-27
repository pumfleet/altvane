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
#include <altvane_ui/util/spring.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "../PluginProcessor.hpp"

namespace altvane::deesser {
    /**
     * the last few seconds, scrolling right to left: the full signal's level, the detection band's level over it,
     * what was taken from each ess (in the sibilance colour, bitten out of the band), the gain reduction hanging
     * from the top, and the threshold, which drags
     */
    class HistoryView final : public juce::Component, public juce::SettableTooltipClient {
    public:
        HistoryView(PluginProcessor& p, ui::ParamEditor& editor);

        void tick(double dt);

        void setFrozen(bool frozen);

        bool isFrozen() const { return frozen_; }

        void setSeconds(double seconds);

        double getSeconds() const { return seconds_; }

        /**
         * the parameter under the mouse (the threshold), or -1
         */
        int getHoveredParameterIndex() const;

        float getThresholdY() const { return dbToY(threshold_db_); }

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;

    private:
        static constexpr size_t kCapacity = 2048;

        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        std::vector<dsp::HistoryFrame> frames_;
        uint64_t count_{0};
        double seconds_{4.0};
        bool frozen_{false};
        std::array<ui::Chip, 3> spans_{ui::Chip{"2s"}, ui::Chip{"4s"}, ui::Chip{"8s"}};
        ui::IconButton freeze_{ui::icons::Icon::kFreeze};

        juce::Rectangle<float> plot_;
        juce::Image grid_;
        float threshold_db_{-24.f}, range_db_{9.f};
        ui::Fade crossing_{.12f};
        double quiet_seconds_{10.0};
        bool hover_threshold_{false}, dragging_{false};
        float drag_start_db_{0.f}, drag_start_y_{0.f};
        uint64_t edit_id_{0};
        std::optional<float> hover_x_;

        float dbToY(float db) const;
        float yToDb(float y) const;
        float reductionY(float db) const;
        juce::RangedAudioParameter* thresholdParam() const;
        bool nearThreshold(juce::Point<float> p) const;
        void rebuildGrid();
        void updateTooltip();
        void paintReadout(juce::Graphics& g, float x, double frames_per_pixel);
    };
}
