// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../theme/look_and_feel.hpp"
#include "../session/param_editor.hpp"
#include "../session/preset_manager.hpp"
#include "../session/session.hpp"
#include "../session/ui_settings.hpp"
#include "header.hpp"
#include "preset_browser.hpp"
#include "settings_sheet.hpp"

namespace altvane::ui {
    /**
     * what a product tells the shared editor shell about itself
     */
    struct ShellConfig {
        struct Window {
            // logical pixels, before the interface scale
            int default_width, default_height, min_width, min_height, max_width{3200}, max_height{2000};
        };

        struct SettingSwitch {
            juce::String section, label, key;
            bool default_value;
        };

        // drawn after the brand mark in the header, e.g. "EQ"
        juce::String product;
        Window window;
        PresetCodec preset_codec;
        // the about line in settings
        juce::String credit;
        std::vector<SettingSwitch> switches;
    };

    /**
     * the part of every Altvane editor that is not the product:
     * look-and-feel, interface scale and window size, the frame loop, header, presets, settings,
     * undo keys, the host's parameter menu and test hooks
     *
     * products lay out their own components in root, below the header, and get one tick per frame;
     * the derived constructor must call finishSetup() once its components exist
     */
    class EditorShell : public juce::AudioProcessorEditor {
    public:
        EditorShell(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& parameters, Session& session,
                    ShellConfig config);

        ~EditorShell() override;

        void paint(juce::Graphics& g) override;

        void resized() override;

        void visibilityChanged() override;

        void parentHierarchyChanged() override;

        bool keyPressed(const juce::KeyPress& key) override;

        /**
         * the parameter of the closest component (or ancestor) whose component ID is a parameter ID
         */
        int getControlParameterIndex(juce::Component& component) override;

        /**
         * drive the editor without a window (snapshot tool, tests)
         */
        void runFrameForTesting(double timestamp);

        void openPresetsForTesting() { header_.on_open_presets(); }

        void openSettingsForTesting() { header_.on_open_settings(); }

        void closeSheetsForTesting() {
            preset_browser_.close();
            settings_sheet_.close();
        }

        const UISettings& getUISettings() const { return ui_settings_; }

    protected:
        LookAndFeel laf_;
        ParamEditor param_editor_;
        PresetManager presets_;
        juce::Component root_;
        Header header_;
        PresetBrowser preset_browser_;
        SettingsSheet settings_sheet_;

        /**
         * place the product's components; area is root_, in logical pixels, below the header
         */
        virtual void layout(juce::Rectangle<int> area) = 0;

        /**
         * once per frame, before the header and sheets
         */
        virtual void tick(double dt) = 0;

        /**
         * the editor became visible (or hidden): start or stop the product's analysis
         */
        virtual void showingChanged(bool) {
        }

        /**
         * the user changed a preference; the accent, motion and input settings are applied already
         */
        virtual void settingsApplied() {
        }

        /**
         * where keyboard focus goes when the editor opens or a sheet closes
         */
        virtual juce::Component* getDefaultFocus() { return nullptr; }

        /**
         * call at the end of the derived constructor, once every component is added to root_
         */
        void finishSetup();

        /**
         * stop the frame loop; call first in the derived destructor
         */
        void stopFrames();

        double getFrameRate() const { return frame_rate_; }

    private:
        juce::AudioProcessor& processor_;
        juce::AudioProcessorValueTreeState& parameters_;
        ShellConfig config_;
        std::unique_ptr<juce::PropertiesFile> settings_;
        UISettings ui_settings_;
        juce::TooltipWindow tooltip_window_{&root_, 700};

        std::unique_ptr<juce::VBlankAttachment> vblank_;
        double last_timestamp_{0.0};
        double frame_rate_{60.0};
        bool is_showing_{false};
        bool applying_size_{false};

        void frame(double timestamp);
        void updateShowing();
        void focusDefault();
        void setInterfaceScale(float scale);
        void applySettings(const UISettings& settings);
        float getScale() const { return ui_settings_.scale; }
        void resetWindowSize();
    };
}
