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
#include "model/character.hpp"

namespace altvane::limiter {
    /**
     * the floating control card: character, gain and ceiling, timing and stereo link
     */
    class ControlCard final : public juce::Component {
    public:
        std::function<void()> on_collapse;

        ControlCard(PluginProcessor& p, ui::ParamEditor& editor);

        void tick();

        /**
         * the width the card needs at its height
         */
        static float getPreferredWidth();

        /**
         * set the timing controls to one of kCharacters, as one undo step
         */
        void applyCharacter(size_t index);

        juce::String getCharacterName() const { return character_name_; }

        void paint(juce::Graphics& g) override;

        void resized() override;

        ui::Knob& getGainKnobForTesting() { return gain_; }
        ui::Slider& getLookaheadForTesting() { return lookahead_; }

    private:
        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        ui::Selector character_;
        ui::Knob gain_, ceiling_, attack_, release_, recovery_;
        ui::Slider lookahead_, link_;
        ui::Chip true_peak_{"True peak"}, unity_{"Unity gain"};
        ui::IconButton collapse_{ui::icons::Icon::kChevronDown};
        juce::String character_name_, character_text_;
        juce::Rectangle<float> character_caption_, description_;
        std::array<float, 2> dividers_{};
    };
}
