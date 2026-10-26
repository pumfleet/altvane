// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "common.hpp"
#include "altvane_ui/shell/editor_shell.hpp"
#include "../PluginProcessor.hpp"
#include "model/engine_bridge.hpp"
#include "model/selection.hpp"
#include "analysis/response_model.hpp"
#include "analysis/spectrum_worker.hpp"
#include "graph/graph_view.hpp"
#include "panels/band_panel.hpp"
#include "panels/chrome.hpp"
#include "panels/match_panel.hpp"
#include "analysis/match_engine.hpp"

namespace altvane::eq {
    /**
     * the Altvane EQ editor
     * everything is laid out in logical pixels inside root_, which is scaled by the interface size setting
     */
    class Editor final : public EditorShell {
    public:
        explicit Editor(PluginProcessor& p);

        ~Editor() override;

        bool keyPressed(const juce::KeyPress& key) override;

        int getControlParameterIndex(juce::Component& component) override;

        Selection& getSelectionForTesting() { return selection_; }

        GraphView& getGraphForTesting() { return graph_; }

        MatchEngine& getMatchForTesting() { return match_; }

    private:
        PluginProcessor& p_ref_;
        EngineBridge bridge_;
        Selection selection_;
        ResponseModel response_;
        MatchEngine match_;
        SpectrumWorker spectrum_;

        GraphView graph_;
        BandPanel band_panel_;
        Footer footer_;
        OutputRail rail_;
        MatchPanel match_panel_;

        Spring panel_x_{0.f, 6.f};
        Fade panel_fade_{.16f};
        Fade match_fade_{.16f};
        bool panel_was_visible_{false};

        void layout(juce::Rectangle<int> area) override;
        void tick(double dt) override;
        void showingChanged(bool showing) override;
        void settingsApplied() override;
        juce::Component* getDefaultFocus() override { return &graph_; }

        void layoutPanel(double dt);
        void layoutMatchPanel(double dt);
        void toggleMatch();
    };
}
