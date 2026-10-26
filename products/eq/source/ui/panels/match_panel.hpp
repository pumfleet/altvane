// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "altvane_ui/widgets/widgets.hpp"
#include "../analysis/match_engine.hpp"

namespace altvane::eq {
    /**
     * guided EQ Match: choose a reference, let it listen, adjust the correction, match, pick the number of bands
     */
    class MatchPanel final : public juce::Component, private juce::Timer {
    public:
        explicit MatchPanel(MatchEngine& engine);

        ~MatchPanel() override;

        void sync();

        int getIdealWidth() const { return 640; }

        int getIdealHeight() const { return 128; }

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        MatchEngine& engine_;
        Selector reference_, limit_;
        ValueField amount_, tilt_, bands_;
        PillButton relearn_{"Relearn"}, cancel_{"Cancel"}, primary_{"Match", true};
        IconButton close_{icons::Icon::kClose};
        std::unique_ptr<juce::FileChooser> chooser_;
        juce::String status_, hint_;

        void timerCallback() override;

        void updateStatus();
    };
}
