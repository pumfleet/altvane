// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../util/spring.hpp"
#include "widgets.hpp"

namespace altvane::ui {
    /**
     * a modal card over the editor: dimmed backdrop, fade and slide in, closes on Esc or a click outside
     */
    class Sheet : public juce::Component {
    public:
        std::function<void()> on_closed;

        explicit Sheet(const juce::String& title);

        void open();

        void close();

        bool isOpen() const { return open_; }

        /**
         * advance the transition; returns true while visible
         */
        bool tick(float dt);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseDown(const juce::MouseEvent& e) override;

        bool keyPressed(const juce::KeyPress& key) override;

    protected:
        juce::String title_;
        juce::Rectangle<int> card_;

        virtual juce::Point<int> getCardSize() const { return {760, 480}; }

        /**
         * lay out child components inside the card, below the title row
         */
        virtual void layoutContent(juce::Rectangle<int> area) = 0;

        virtual void opened() {
        }

    private:
        Fade fade_{.18f};
        bool open_{false};
        IconButton close_{icons::Icon::kClose};
    };
}
