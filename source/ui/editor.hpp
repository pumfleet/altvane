// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../PluginProcessor.hpp"
#include "model/engine_bridge.hpp"
#include "model/selection.hpp"
#include "analysis/response_model.hpp"
#include "analysis/spectrum_worker.hpp"
#include "graph/graph_view.hpp"
#include "panels/band_panel.hpp"
#include "panels/chrome.hpp"
#include "panels/preset_browser.hpp"
#include "panels/settings_sheet.hpp"
#include "panels/match_panel.hpp"
#include "analysis/match_engine.hpp"
#include "model/preset_manager.hpp"
#include "model/ui_settings.hpp"
#include "theme/look_and_feel.hpp"

namespace contour::ui {
    /**
     * the Contour editor
     * everything is laid out in logical pixels inside root_, which is scaled by the interface size setting
     */
    class Editor final : public juce::AudioProcessorEditor {
    public:
        explicit Editor(PluginProcessor& p);

        ~Editor() override;

        void paint(juce::Graphics& g) override;

        void resized() override;

        void visibilityChanged() override;

        void parentHierarchyChanged() override;

        bool keyPressed(const juce::KeyPress& key) override;

        int getControlParameterIndex(juce::Component& component) override;

        /**
         * drive the editor without a window (snapshot tool, tests)
         */
        void runFrameForTesting(double timestamp);

        Selection& getSelectionForTesting() { return selection_; }

        GraphView& getGraphForTesting() { return graph_; }

        MatchEngine& getMatchForTesting() { return match_; }

        void openPresetsForTesting() { header_.on_open_presets(); }

        void openSettingsForTesting() { header_.on_open_settings(); }

        void closeSheetsForTesting() {
            preset_browser_.close();
            settings_sheet_.close();
        }

    private:
        PluginProcessor& p_ref_;
        LookAndFeel laf_;
        EngineBridge bridge_;
        Selection selection_;
        ResponseModel response_;
        MatchEngine match_;
        SpectrumWorker spectrum_;
        PresetManager presets_;
        UISettings ui_settings_;

        juce::Component root_;
        GraphView graph_;
        BandPanel band_panel_;
        Header header_;
        Footer footer_;
        OutputRail rail_;
        MatchPanel match_panel_;
        PresetBrowser preset_browser_;
        SettingsSheet settings_sheet_;
        juce::TooltipWindow tooltip_window_{&root_, 700};

        std::unique_ptr<juce::VBlankAttachment> vblank_;
        double last_timestamp_{0.0};
        double frame_rate_{60.0};
        bool is_showing_{false};

        Spring panel_x_{0.f, 6.f};
        Fade panel_fade_{.16f};
        Fade match_fade_{.16f};
        bool panel_was_visible_{false};

        std::unique_ptr<juce::PropertiesFile> settings_;
        bool applying_size_{false};


        void frame(double timestamp);
        void layoutPanel(double dt);
        void layoutMatchPanel(double dt);
        void toggleMatch();
        void updateShowing();
        void setInterfaceScale(float scale);
        void applySettings(const UISettings& settings);
        float getScale() const { return ui_settings_.scale; }
        void resetWindowSize();
    };
}
