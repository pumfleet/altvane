// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../widgets/widgets.hpp"
#include "../session/param_editor.hpp"
#include "../session/preset_manager.hpp"
#include "../session/session.hpp"

namespace altvane::ui {
    /**
     * top bar: identity, preset, A/B, undo / redo, settings
     */
    class Header final : public juce::Component {
    public:
        std::function<void()> on_undo, on_redo, on_open_presets, on_open_settings;

        /**
         * @param product the short product name drawn after the brand mark, e.g. "EQ"
         */
        Header(const juce::String& product, ParamEditor& editor, Session& session,
               juce::AudioProcessorValueTreeState& parameters, PresetManager& presets);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseUp(const juce::MouseEvent& e) override;

    private:
        juce::String product_;
        ParamEditor& editor_;
        Session& session_;
        juce::AudioProcessorValueTreeState& parameters_;
        PresetManager& presets_;
        IconButton undo_{icons::Icon::kUndo}, redo_{icons::Icon::kRedo}, settings_{icons::Icon::kSettings};
        IconButton prev_{icons::Icon::kChevronLeft}, next_{icons::Icon::kChevronRight};
        Chip slot_a_{"A"}, slot_b_{"B"};
        IconButton copy_slot_{icons::Icon::kCopy};
        juce::String preset_name_;
        bool preset_modified_{false};
        int modified_check_countdown_{0};
        juce::Rectangle<float> preset_area_;
        std::unique_ptr<juce::FileChooser> chooser_;
        bool preset_hovered_{false};

        void selectSlot(int slot);
        void showPresetMenu();
        void savePresetAs();
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
    };
}
