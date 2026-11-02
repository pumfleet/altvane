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
     * bottom bar: quality, stereo mode, the removed-audio audition, latency, output and bypass
     */
    class Footer final : public juce::Component {
    public:
        Footer(PluginProcessor& p, ui::ParamEditor& editor);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        ui::Chip eco_{"Eco"}, high_{"High"}, linked_{"Linked"}, stereo_{"Stereo"}, mid_side_{"M/S"};
        ui::Chip removed_{"Removed"};
        ui::ValueField output_;
        ui::IconButton bypass_{ui::icons::Icon::kPower};
        juce::Rectangle<float> quality_label_, stereo_label_, latency_area_;
        juce::String latency_text_;
        bool show_stereo_{true};

        void setChoice(const char* id, int index, const juce::String& name);
    };
}
