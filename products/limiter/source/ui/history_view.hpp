// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/util/spring.hpp>

#include "common.hpp"
#include "model/level_feed.hpp"

namespace altvane::limiter {
    /**
     * the scrolling level history: the gained input as a soft fill, the output over it, gain reduction hanging
     * from the top, short-term loudness against the target band, and the ceiling as a line to drag
     * dragging anywhere else moves the gain, so the waveform can be pushed into the ceiling directly
     */
    class HistoryView final : public juce::Component {
    public:
        HistoryView(PluginProcessor& p, ui::ParamEditor& editor, const LevelFeed& feed);

        /**
         * once per frame, after the feed was pulled
         */
        void tick(double dt, size_t new_columns, const LoudnessReadings& loudness);

        /**
         * room taken by the floating panels: the plot stays clear of them
         */
        void setInsets(float right, float bottom);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;

        /**
         * the parameter under the mouse (ceiling near its line, gain elsewhere), for the host's menu
         */
        int getHoveredParameterIndex() const;

        // for tests and the snapshot tool
        juce::Rectangle<float> getPlotArea() const { return plot_; }
        float yOfDB(float db) const;
        void setHoverForTesting(std::optional<juce::Point<float>> p) { hover_ = p; repaint(); }

    private:
        enum class Drag { kNone, kGain, kCeiling };

        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        const LevelFeed& feed_;
        juce::RangedAudioParameter *gain_, *ceiling_;

        juce::Rectangle<float> plot_;
        ui::Spring inset_right_{0.f, 7.f}, inset_bottom_{0.f, 7.f};
        float min_db_{-36.f}, seconds_{9.f};
        float ceiling_db_{-.1f}, gain_db_{0.f};
        std::optional<float> target_lufs_;
        bool bypassed_{false}, delta_{false}, true_peak_{true};

        std::optional<juce::Point<float>> hover_;
        bool ceiling_hot_{false};
        Drag drag_{Drag::kNone};
        float drag_last_y_{0.f}, drag_gain_{0.f};

        // per pixel, newest on the right
        std::vector<float> px_input_, px_output_, px_reduction_, px_loudness_;
        size_t px_count_{0};

        float dbOfY(float y) const;
        void layoutPlot();
        void aggregate();
        void paintGrid(juce::Graphics& g);
        void paintCeiling(juce::Graphics& g) const;
        void paintLegend(juce::Graphics& g) const;
        void paintHover(juce::Graphics& g) const;
        void paintDragTag(juce::Graphics& g) const;
        bool nearCeiling(juce::Point<float> p) const;
    };
}
