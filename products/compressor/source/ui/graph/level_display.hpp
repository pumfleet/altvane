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
     * the scrolling level display: input as a filled area, output as a line, the gain change hanging from the top,
     * and the threshold as a line that can be dragged; hovering reads out any moment of the history
     */
    class LevelDisplay final : public juce::Component, private juce::Timer {
    public:
        LevelDisplay(CompBridge& bridge, LevelHistory& history);

        ~LevelDisplay() override;

        /**
         * the history ends here (the knee display covers the rest); the dB labels sit just right of it
         */
        void setPlotRight(float x);

        int getPlotWidth() const;

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    private:
        static constexpr float kPadTop = 14.f, kPadBottom = 10.f;

        CompBridge& bridge_;
        LevelHistory& history_;
        float plot_right_{0.f};
        float scale_db_{54.f};
        std::optional<juce::Point<float>> hover_;
        bool threshold_hover_{false}, dragging_threshold_{false};
        juce::Path input_path_, output_path_, gain_fill_, gain_line_;
        // the parameter a burst of wheel events is editing
        juce::String wheel_id_;

        float yOf(float db) const;
        float dbOf(float y) const;
        float gainY(float db) const;
        bool isNearThreshold(juce::Point<float> p) const;
        void buildPaths();
        void drawGrid(juce::Graphics& g) const;
        void drawThreshold(juce::Graphics& g) const;
        void drawLegend(juce::Graphics& g) const;
        void drawHover(juce::Graphics& g) const;
        void timerCallback() override;
    };
}
