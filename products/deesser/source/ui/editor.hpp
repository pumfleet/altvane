// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/shell/editor_shell.hpp>

#include "analyzer.hpp"
#include "band_view.hpp"
#include "chrome.hpp"
#include "control_deck.hpp"
#include "history_view.hpp"

namespace altvane::deesser {
    class PluginProcessor;

    /**
     * the Altvane De-Esser editor: the level history on top; the detection band and the controls below; the
     * per-track settings in the footer; the meters on the right.
     * everything is laid out in logical pixels inside root_, which is scaled by the interface size setting
     */
    class Editor final : public ui::EditorShell {
    public:
        explicit Editor(PluginProcessor& p);

        ~Editor() override;

        int getControlParameterIndex(juce::Component& component) override;

        bool keyPressed(const juce::KeyPress& key) override;

        HistoryView& getHistoryForTesting() { return history_; }

        BandView& getBandViewForTesting() { return band_view_; }

        ControlDeck& getDeckForTesting() { return deck_; }

        Footer& getFooterForTesting() { return footer_; }

        Analyzer& getAnalyzerForTesting() { return analyzer_; }

    private:
        PluginProcessor& p_ref_;
        Analyzer analyzer_;
        HistoryView history_;
        BandView band_view_;
        ControlDeck deck_;
        Footer footer_;
        MeterRail rail_;

        void layout(juce::Rectangle<int> area) override;
        void tick(double dt) override;
        void showingChanged(bool showing) override;
        juce::Component* getDefaultFocus() override { return &deck_.getThreshold(); }
    };
}
