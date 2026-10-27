// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "../PluginProcessor.hpp"

namespace altvane::leveler {
    /**
     * the controls below the history: the target, how the rider moves and listens, learn and lookahead
     */
    class ControlDeck final : public juce::Component {
    public:
        ControlDeck(PluginProcessor& p, ui::ParamEditor& editor);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        ui::Knob& getTarget() { return target_; }

        ui::Knob& getKnob(const char* id);

        ui::PillButton& getLearnButton() { return learn_; }

        ui::Chip& getLookahead() { return lookahead_; }

    private:
        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        ui::Knob target_, speed_, sensitivity_, guard_, music_;
        ui::PillButton learn_{"Learn"};
        ui::Chip lookahead_{"Lookahead"};
        juce::Rectangle<float> divider_;
        juce::String learn_message_;
        juce::Rectangle<float> message_area_;

        void toggleLearn();
    };
}
