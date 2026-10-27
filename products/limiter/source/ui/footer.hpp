// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "common.hpp"

namespace altvane::limiter {
    /**
     * bottom bar: oversampling, the output stage (dither, DC filter), audition, the display's time and range,
     * the loudness scale, latency and bypass
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
        ui::Selector oversampling_, dither_, history_, range_, scale_;
        ui::Chip shaped_{"Shaped"}, dc_{"DC filter"}, audition_{"Audition"};
        ui::IconButton bypass_{ui::icons::Icon::kPower};
        juce::Rectangle<float> display_label_, latency_area_;
        float separator_x_{0.f};
        juce::String latency_text_;

        void setupChoice(ui::Selector& selector, juce::AudioProcessorValueTreeState& tree, const char* id,
                         const juce::String& name, bool undoable, const juce::StringArray& labels = {});
    };
}
