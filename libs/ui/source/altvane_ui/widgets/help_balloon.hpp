// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace altvane::ui {
    /**
     * give a component a help balloon: a title and a sentence on what it does
     * (also set as its accessibility help text)
     */
    void setHelp(juce::Component& component, const juce::String& title, const juce::String& text);

    /**
     * shows the help of the component under the mouse after a short rest, in a balloon over the editor
     * add it as the last child of the editor's root so it draws on top; it never takes mouse clicks
     */
    class HelpBalloon final : public juce::Component, private juce::Timer {
    public:
        static constexpr int kDelayMs = 450;

        explicit HelpBalloon(juce::Component& root);

        ~HelpBalloon() override;

        void setActive(bool active);

        bool isActive() const { return active_; }

        /**
         * show the help of a component now (snapshot tool, tests)
         */
        void showFor(juce::Component& target);

        void hide();

        void paint(juce::Graphics& g) override;

    private:
        juce::Component& root_;
        bool active_{true};
        juce::Component::SafePointer<juce::Component> candidate_, shown_;
        double candidate_since_{0.0};
        juce::String title_, text_;
        float alpha_{0.f};

        void timerCallback() override;
        void place(juce::Component& target);
        static juce::Component* findHelpTarget(juce::Component* c, const juce::Component& root);
    };
}
