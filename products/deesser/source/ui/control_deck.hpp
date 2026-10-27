// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/util/spring.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "../PluginProcessor.hpp"
#include "../dsp/learner.hpp"

namespace altvane::deesser {
    /**
     * the controls touched every session: threshold (with Learn), range, how sibilance is detected and taken
     * down, and what to listen to
     */
    class ControlDeck final : public juce::Component {
    public:
        ControlDeck(PluginProcessor& p, ui::ParamEditor& editor);

        void tick(double dt);

        /**
         * start listening for esses, or stop and set the threshold from what was heard
         */
        void toggleLearn();

        void cancelLearn();

        bool isLearning() const { return learner_.isLearning(); }

        ui::Knob& getThreshold() { return threshold_; }

        ui::Knob& getRange() { return range_; }

        ui::Chip& getLearn() { return learn_; }

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        struct Row {
            juce::String caption;
            juce::Rectangle<float> caption_area;
            std::vector<ui::Chip*> chips;
        };

        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        ui::Knob threshold_, range_;
        ui::Chip learn_{"Learn"};
        ui::Chip vocal_{"Vocal"}, allround_{"Allround"}, wide_{"Wide"}, split_{"Split"}, shelf_{"Shelf"}, bell_{"Bell"};
        ui::Chip trigger_{"Trigger"}, delta_{"Delta"};
        std::array<Row, 4> rows_;
        juce::Rectangle<float> divider_;

        dsp::ThresholdLearner learner_;
        // heard enough, waiting for the user to let go of whatever they are dragging before setting the threshold
        bool learn_due_{false};
        juce::String learn_message_;
        double message_age_{0.0}, pulse_{0.0};
        ui::Fade learn_fade_{.2f};

        void setChoice(const char* id, int index, const juce::String& name);
        void finishLearn();
        void updateLearnText();
    };
}
