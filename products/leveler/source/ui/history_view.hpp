// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <optional>
#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/widgets/history.hpp>

#include "../PluginProcessor.hpp"

namespace altvane::leveler {
    /**
     * the last seconds of the ride: input, output and music levels against the target and the sensitivity floor
     * (both draggable), the gain below, and what the detector heard in a strip between them
     */
    class HistoryView final : public juce::Component {
    public:
        HistoryView(PluginProcessor& p, ui::ParamEditor& editor);

        /**
         * take the columns the engine queued since the last frame
         */
        void tick();

        void clear();

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;

        void mouseExit(const juce::MouseEvent& e) override;

        void mouseDown(const juce::MouseEvent& e) override;

        void mouseDrag(const juce::MouseEvent& e) override;

        void mouseUp(const juce::MouseEvent& e) override;

        void mouseDoubleClick(const juce::MouseEvent& e) override;

        /**
         * the parameter of the line under the mouse (target or sensitivity), -1 if none
         */
        int getHoveredParameterIndex() const;

        size_t getColumnCountForTesting() const { return ring_.size(); }

    private:
        enum class Line { kNone, kTarget, kFloor };

        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        ui::HistoryRing<dsp::Column, dsp::Engine::kColumns> ring_;
        ui::TimeAxis time_;
        ui::LevelAxis level_, gain_;
        juce::Rectangle<float> plot_, level_area_, strip_area_, gain_area_;
        Line hover_line_{Line::kNone}, drag_line_{Line::kNone};
        std::optional<float> hover_x_;
        std::array<float, 6> look_{};

        float targetDb() const;
        float floorDb() const;
        float gainScale() const;
        Line lineAt(juce::Point<float> position) const;
        juce::RangedAudioParameter* parameterFor(Line line) const;
        void setLineFromY(Line line, float y);
        void drawReadout(juce::Graphics& g, float x) const;
    };
}
