// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace contour::ui {
    /**
     * restyles the few stock JUCE surfaces the UI still uses: popup menus, tooltips and text editors
     */
    class LookAndFeel final : public juce::LookAndFeel_V4 {
    public:
        LookAndFeel();

        /**
         * re-apply colours that derive from the accent
         */
        void refreshAccent();

        juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override;

        // popup menus
        void drawPopupMenuBackgroundWithOptions(juce::Graphics& g, int width, int height,
                                                const juce::PopupMenu::Options&) override;

        void drawPopupMenuItemWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area, bool is_highlighted,
                                          const juce::PopupMenu::Item& item,
                                          const juce::PopupMenu::Options&) override;

        void drawPopupMenuSectionHeaderWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                   const juce::String& section_name,
                                                   const juce::PopupMenu::Options&) override;

        void getIdealPopupMenuItemSizeWithOptions(const juce::String& text, bool is_separator,
                                                  int standard_menu_item_height, int& ideal_width,
                                                  int& ideal_height, const juce::PopupMenu::Options&) override;

        juce::Font getPopupMenuFont() override;

        int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override { return 5; }

        int getMenuWindowFlags() override { return 0; }

        // tooltips
        juce::Rectangle<int> getTooltipBounds(const juce::String& tip_text, juce::Point<int> screen_pos,
                                              juce::Rectangle<int> parent_area) override;

        void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;

        // text editors (inline value entry)
        void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor&) override;

        void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor&) override;

        juce::CaretComponent* createCaretComponent(juce::Component* key_focus_owner) override;

        // scrollbars: a thin rounded thumb, no track
        void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                           bool is_vertical, int thumb_start, int thumb_size, bool is_mouse_over,
                           bool is_mouse_down) override;

        int getDefaultScrollbarWidth() override { return 10; }

        bool areScrollbarButtonsVisible() override { return false; }

        // window resize grip: three quiet diagonal strokes
        void drawCornerResizer(juce::Graphics& g, int w, int h, bool is_mouse_over, bool is_mouse_dragging) override;
    };
}
