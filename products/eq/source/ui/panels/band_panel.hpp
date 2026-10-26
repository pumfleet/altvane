// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "altvane_ui/util/spring.hpp"
#include "altvane_ui/widgets/widgets.hpp"
#include "../model/engine_bridge.hpp"
#include "../model/selection.hpp"

namespace altvane::eq {
    /**
     * contextual controls for the selected band(s), floating over the bottom of the graph
     */
    class BandPanel final : public juce::Component {
    public:
        std::function<void(size_t, bool)> on_solo; // band (kBandNum = off), side-chain
        std::function<size_t()> get_solo;
        std::function<bool()> get_solo_side;

        BandPanel(EngineBridge& bridge, Selection& selection);

        /**
         * sync controls with the model; returns whether the panel wants to be shown
         */
        bool tick();

        int getIdealHeight() const;

        int getIdealWidth() const;

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        enum class Field { kFreq, kGain, kQ, kRange, kThreshold, kKnee, kAttack, kRelease };

        struct EditStart {
            size_t band;
            float value, gain, target;
        };

        EngineBridge& bridge_;
        Selection& selection_;
        size_t band_{kBandNum};
        size_t count_{0};
        std::uint32_t seen_version_{~0u};

        IconButton prev_{icons::Icon::kChevronLeft}, next_{icons::Icon::kChevronRight};
        IconButton dynamic_{icons::Icon::kDynamic}, solo_{icons::Icon::kHeadphones};
        IconButton bypass_{icons::Icon::kPower}, delete_{icons::Icon::kTrash};
        Selector shape_, slope_, stereo_;
        Knob freq_, gain_, q_;
        Knob range_, threshold_, knee_, attack_, release_;
        Chip learn_{"Learn"}, relative_{"Relative"};
        Selector side_type_, side_slope_;
        ValueField side_freq_, side_q_;
        Chip link_{"Link"}, swap_{"Swap"};
        IconButton side_solo_{icons::Icon::kHeadphones};

        std::vector<EditStart> edit_starts_;
        double solo_press_time_{0.0};
        bool solo_latched_{false};

        void bindKnob(Knob& knob, Field field, const juce::String& label);
        void sync();
        std::vector<size_t> targets() const;
        BandParams::Field paramField(Field f) const;
        void step(bool forwards);
        void bindSideField(ValueField& field, BandParams::Field f, const juce::String& label);
    };
}
