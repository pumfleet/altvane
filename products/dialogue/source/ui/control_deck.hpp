// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "../PluginProcessor.hpp"

namespace altvane::dialogue {
    /**
     * one stem: its level, solo (listen to it alone) and its sensitivity curve
     */
    class StemCard final : public juce::Component {
    public:
        std::function<void()> on_solo, on_curve;

        StemCard(size_t stem, PluginProcessor& p, ui::ParamEditor& editor);

        void sync(dsp::Listen listen, int focus);

        void paint(juce::Graphics& g) override;

        void resized() override;

        ui::Knob& getKnob() { return knob_; }

    private:
        size_t stem_;
        PluginProcessor& p_ref_;
        ui::Knob knob_;
        ui::IconButton solo_{ui::icons::Icon::kHeadphones}, curve_{ui::icons::Icon::kCurve};
        bool focused_{false}, sensitivity_changed_{false};
    };

    /**
     * the three stems side by side
     */
    class ControlDeck final : public juce::Component {
    public:
        std::function<void(int)> on_focus;

        ControlDeck(PluginProcessor& p, ui::ParamEditor& editor);

        void tick(int focus);

        void paint(juce::Graphics& g) override;

        void resized() override;

        StemCard& getCard(const size_t stem) { return *cards_[stem]; }

    private:
        PluginProcessor& p_ref_;
        std::array<std::unique_ptr<StemCard>, dsp::kNumStems> cards_;
        int focus_{-1};
    };
}
