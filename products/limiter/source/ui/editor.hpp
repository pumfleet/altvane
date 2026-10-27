// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <altvane_ui/shell/editor_shell.hpp>
#include <altvane_ui/widgets/help_balloon.hpp>

#include "control_card.hpp"
#include "footer.hpp"
#include "history_view.hpp"
#include "loudness_panel.hpp"
#include "meter_rail.hpp"
#include "model/level_feed.hpp"

namespace altvane::limiter {
    /**
     * a vertical tab on the edge of the display that opens and closes the loudness panel
     */
    class PanelTab final : public juce::Component {
    public:
        std::function<void()> on_click;

        explicit PanelTab(const juce::String& text);

        void setOpen(bool open);

        void paint(juce::Graphics& g) override;

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        juce::String text_;
        bool hovered_{false}, open_{true};
    };

    /**
     * the Altvane Limiter editor: the level history fills the window, the loudness panel and the control card
     * float over it, meters on the right, global settings in the footer
     * everything is laid out in logical pixels inside root_, which is scaled by the interface size setting
     */
    class Editor final : public ui::EditorShell {
    public:
        static constexpr auto kHelpSwitch = "help_balloons";

        explicit Editor(PluginProcessor& p);

        ~Editor() override;

        int getControlParameterIndex(juce::Component& component) override;

        HistoryView& getHistoryForTesting() { return history_; }
        ControlCard& getCardForTesting() { return card_; }
        LoudnessPanel& getLoudnessForTesting() { return loudness_; }
        ui::HelpBalloon& getHelpForTesting() { return help_; }
        Footer& getFooterForTesting() { return footer_; }

        void setLoudnessOpen(bool open);
        void setControlsOpen(bool open);

    private:
        PluginProcessor& p_ref_;
        LevelFeed feed_;
        HistoryView history_;
        MeterRail rail_;
        ControlCard card_;
        LoudnessPanel loudness_;
        Footer footer_;
        PanelTab loudness_tab_{"LOUDNESS"};
        ui::PillButton show_controls_{"Show controls"};
        ui::HelpBalloon help_;
        bool loudness_open_{true}, controls_open_{true}, custom_target_{false};
        juce::Rectangle<int> display_;

        void layout(juce::Rectangle<int> area) override;
        void tick(double dt) override;
        void showingChanged(bool showing) override;
        void settingsApplied() override;
        juce::Component* getDefaultFocus() override { return &history_; }
        void placeOverlays();
    };
}
