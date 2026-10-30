// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "altvane_ui/widgets/widgets.hpp"
#include "../model/comp_bridge.hpp"

namespace altvane::compressor {
    /**
     * compress / inflate / expand / shape as four small curve icons
     */
    class DirectionPicker final : public juce::Component {
    public:
        std::function<void(int)> on_change;

        DirectionPicker();

        void setSelected(int index);

        int getSelected() const { return selected_; }

        void resized() override;

        void paint(juce::Graphics& g) override;

    private:
        class Segment;

        std::vector<std::unique_ptr<Segment>> segments_;
        int selected_{0};
    };

    /**
     * the main controls, on a glass card floating over the display:
     * style and direction, threshold and ratio with knee and range, attack and release with hold and lookahead,
     * makeup (with loudness Learn) and wet
     */
    class ControlCard final : public juce::Component {
    public:
        // the More button opens the advanced controls
        std::function<void()> on_more;

        explicit ControlCard(CompBridge& bridge);

        ~ControlCard() override;

        static juce::Point<int> getIdealSize() { return {901, 128}; }

        /**
         * @param side_db the detector level, shown as a ring around the threshold knob
         */
        void tick(double dt, std::optional<float> side_db);

        void setMoreOpen(bool open) { more_.setOn(open); }

        juce::Component& getMoreButton() { return more_; }

        /**
         * Learn: the first call starts measuring the input and output loudness, the second sets makeup from them
         */
        void toggleLearn();

        bool isLearning() const { return learning_; }

        void paint(juce::Graphics& g) override;

        void paintOverChildren(juce::Graphics& g) override;

        void resized() override;

    private:
        CompBridge& bridge_;
        ui::Selector style_;
        DirectionPicker direction_;
        ui::Chip more_{"More"};
        ui::Knob threshold_, ratio_, attack_, release_, makeup_, wet_;
        ui::Slider knee_, range_, hold_, lookahead_;
        ui::Chip learn_{"Learn"};
        bool learning_{false};
        float ring_{0.f};
        juce::Rectangle<float> style_caption_, direction_name_;
        std::array<float, 3> dividers_{};

        juce::String getStyleText() const;
    };
}
