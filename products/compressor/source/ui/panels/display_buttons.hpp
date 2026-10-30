// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace altvane::compressor {
    /**
     * a button that floats over the display; the two kinds are the knee tab and the side-chain pill
     */
    class DisplayButton final : public juce::Component {
    public:
        enum class Kind {
            // a vertical "KNEE" tab on the edge of the knee display
            kKneeTab,
            // "SIDE CHAIN" with a glow when it changes the sound, and a chevron for the drawer
            kSideChain
        };

        std::function<void()> on_click;

        explicit DisplayButton(Kind kind);

        void setOpen(bool open);

        void setActive(bool active);

        int getIdealWidth() const;

        void paint(juce::Graphics& g) override;

        void mouseEnter(const juce::MouseEvent&) override { hovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override { hovered_ = false; repaint(); }
        void mouseUp(const juce::MouseEvent& e) override;
        bool keyPressed(const juce::KeyPress& key) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection) override;
        void focusLost(FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    private:
        Kind kind_;
        bool open_{false}, active_{false}, hovered_{false}, focus_ring_{false};
    };
}
