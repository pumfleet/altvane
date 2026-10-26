// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../model/engine_bridge.hpp"
#include "../analysis/spectrum_worker.hpp"
#include "../model/preset_manager.hpp"
#include "../widgets/widgets.hpp"

namespace contour::ui {
    /**
     * top bar: identity, preset, undo / redo, settings
     */
    class Header final : public juce::Component {
    public:
        std::function<void()> on_undo, on_redo, on_open_presets, on_open_settings;
        std::function<juce::PopupMenu()> build_settings_menu;
        std::function<void(int)> on_settings_result;

        Header(EngineBridge& bridge, PresetManager& presets);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseUp(const juce::MouseEvent& e) override;

    private:
        EngineBridge& bridge_;
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

        void showPresetMenu();
        void savePresetAs();
        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
    };

    /**
     * bottom bar: processing mode, analyzer sources, global gain controls
     */
    class Footer final : public juce::Component {
    public:
        std::function<void()> on_toggle_match;
        std::function<bool()> is_matching;

        explicit Footer(EngineBridge& bridge);

        ~Footer() override;

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        EngineBridge& bridge_;
        Selector phase_;
        Chip pre_{"Pre"}, post_{"Post"}, side_spectrum_{"SC"};
        IconButton analyzer_menu_{icons::Icon::kChevronDown};
        Chip match_{"EQ Match"};
        Chip ext_side_{"Side-chain"}, auto_gain_{"Auto Gain"};
        ValueField scale_, output_;
        IconButton output_menu_{icons::Icon::kChevronDown};
        IconButton bypass_{icons::Icon::kPower};
        bool loudness_learning_{false};

        void showAnalyzerMenu();
        void showOutputMenu();
        void setLoudnessLearning(bool on);
        void bindField(ValueField& field, const char* id, const juce::String& name);
    };

    /**
     * right rail: EQ display range and the output meter
     */
    class OutputRail final : public juce::Component {
    public:
        OutputRail(EngineBridge& bridge, SpectrumWorker& spectrum);

        void tick(double dt);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseUp(const juce::MouseEvent& e) override;

    private:
        EngineBridge& bridge_;
        SpectrumWorker& spectrum_;
        std::array<float, 2> levels_{-120.f, -120.f};
        float peak_hold_{-120.f};
        double peak_age_{0.0};
        juce::Rectangle<float> range_area_, meter_area_, peak_area_;
    };
}
