// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/util/spring.hpp>

#include "../PluginProcessor.hpp"

namespace altvane::dialogue {
    /**
     * the live picture of the separation: the input's spectrum split into what each stem keeps (stacked, voice at
     * the bottom), under a faint outline of the input, so lowering a level visibly shrinks its layer.
     * with a stem focused, its sensitivity curve sits on top and its six nodes drag up and down
     */
    class Visualiser final : public juce::Component, public juce::SettableTooltipClient {
    public:
        static constexpr int kNoFocus = -1;

        std::function<void(int)> on_focus_changed;

        Visualiser(PluginProcessor& p, ui::ParamEditor& editor);

        void tick(double dt);

        void setFocus(int stem);

        int getFocus() const { return focus_; }

        /**
         * the parameter under the mouse (a sensitivity node), or -1
         */
        int getHoveredParameterIndex() const;

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;

    private:
        static constexpr int kBands = dsp::AnalysisFrame::kBands;
        using Bands = std::array<float, kBands>;

        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        int focus_{kNoFocus};
        ui::Fade focus_fade_{.16f};

        dsp::AnalysisFrame frame_;
        std::array<Bands, dsp::kNumStems> stem_db_{};
        Bands input_db_{};
        std::array<ui::Spring, dsp::kNumStems> level_springs_;
        double silence_{10.0};
        bool have_signal_{false};

        juce::Rectangle<float> plot_;
        juce::Image grid_;
        std::array<float, dsp::kSensitivityNodes> nodes_{};
        int hovered_node_{-1}, dragged_node_{-1};
        float drag_start_value_{0.f}, drag_start_y_{0.f};

        float bandX(int band) const;
        float freqX(float hz) const;
        float energyY(float db) const;
        float sensitivityY(float value) const;
        float sensitivityValue(float y) const;
        juce::RangedAudioParameter* nodeParam(int node) const;
        int hitNode(juce::Point<float> p) const;
        void rebuildGrid();
        juce::Path makeLevelPath(const Bands& db) const;
        void paintLayers(juce::Graphics& g);
        void paintSensitivity(juce::Graphics& g, float alpha);
        void paintMessage(juce::Graphics& g);
        void updateTooltip();
    };
}
