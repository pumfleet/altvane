// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "altvane_ui/shell/editor_shell.hpp"
#include "altvane_ui/util/spring.hpp"
#include "altvane_ui/widgets/help_balloon.hpp"

#include "analysis/level_history.hpp"
#include "analysis/side_spectrum.hpp"
#include "analysis/transfer_model.hpp"
#include "graph/knee_display.hpp"
#include "graph/level_display.hpp"
#include "model/comp_bridge.hpp"
#include "panels/advanced_popover.hpp"
#include "panels/control_card.hpp"
#include "panels/display_buttons.hpp"
#include "panels/footer.hpp"
#include "panels/meter_rail.hpp"
#include "panels/side_chain_panel.hpp"

class PluginProcessor;

namespace altvane::compressor {
    /**
     * the Altvane Compressor editor (see docs/design/mock.html):
     * the level display with the knee display and the control card floating over it, the meters on the right,
     * the side-chain drawer below and global settings in the footer
     * everything is laid out in logical pixels inside root_, which the interface size setting scales
     */
    class Editor final : public ui::EditorShell {
    public:
        static constexpr auto kHelpSwitch = "help_balloons";

        explicit Editor(PluginProcessor& p);

        ~Editor() override;

        bool keyPressed(const juce::KeyPress& key) override;

        void mouseDown(const juce::MouseEvent& e) override;

        void setAdvancedOpen(bool open);

        ControlCard& getCardForTesting() { return card_; }

        LevelHistory& getHistoryForTesting() { return history_; }

        SideChainPanel& getSideChainForTesting() { return side_panel_; }

        ui::HelpBalloon& getHelpForTesting() { return help_; }

    private:
        PluginProcessor& p_ref_;
        CompBridge bridge_;
        LevelHistory history_;
        TransferModel model_;
        SideSpectrum spectrum_;
        LevelDisplay level_;
        KneeDisplay knee_;
        DisplayButton knee_tab_{DisplayButton::Kind::kKneeTab};
        ControlCard card_;
        DisplayButton side_button_{DisplayButton::Kind::kSideChain};
        AdvancedPopover advanced_;
        MeterRail rail_;
        SideChainPanel side_panel_;
        Footer footer_;
        ui::HelpBalloon help_;
        ui::Fade side_fade_{.2f};
        juce::Rectangle<int> area_;
        bool knee_fits_{true};

        void layout(juce::Rectangle<int> area) override;
        void layoutDisplay();
        void tick(double dt) override;
        void showingChanged(bool showing) override;
        void settingsApplied() override;
        juce::Component* getDefaultFocus() override { return &level_; }
        bool isKneeOpen() const;
        bool isSideChainOpen() const;
    };
}
