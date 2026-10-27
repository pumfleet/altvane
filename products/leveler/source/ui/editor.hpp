// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/shell/editor_shell.hpp>

#include "chrome.hpp"
#include "control_deck.hpp"
#include "history_view.hpp"
#include "rider_panel.hpp"

namespace altvane::leveler {
    class PluginProcessor;

    /**
     * the Altvane Leveler editor: the level history with the rider fader beside it, the controls below
     * everything is laid out in logical pixels inside root_, which is scaled by the interface size setting
     */
    class Editor final : public ui::EditorShell {
    public:
        explicit Editor(PluginProcessor& p);

        ~Editor() override;

        int getControlParameterIndex(juce::Component& component) override;

        HistoryView& getHistoryForTesting() { return history_; }

        RiderPanel& getRiderForTesting() { return rider_; }

        ControlDeck& getDeckForTesting() { return deck_; }

    private:
        PluginProcessor& p_ref_;
        HistoryView history_;
        RiderPanel rider_;
        ControlDeck deck_;
        Footer footer_;
        OutputRail rail_;

        void layout(juce::Rectangle<int> area) override;
        void tick(double dt) override;
        void showingChanged(bool showing) override;
        juce::Component* getDefaultFocus() override { return &deck_.getTarget(); }
    };
}
