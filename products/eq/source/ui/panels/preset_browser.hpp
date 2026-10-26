// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "sheet.hpp"
#include "../model/preset_manager.hpp"

namespace contour::ui {
    /**
     * browse, search, organise and load presets
     */
    class PresetBrowser final : public Sheet,
                                private juce::Timer,
                                private juce::KeyListener {
    public:
        explicit PresetBrowser(PresetManager& presets);

        ~PresetBrowser() override;

    private:
        enum class Entry { kNone, kSaveAs, kRename, kNewGroup };

        class GroupModel final : public juce::ListBoxModel {
        public:
            explicit GroupModel(PresetBrowser& owner) : owner_(owner) {
            }

            int getNumRows() override;
            void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
            void selectedRowsChanged(int last_row) override;

        private:
            PresetBrowser& owner_;
        };

        class PresetModel final : public juce::ListBoxModel {
        public:
            explicit PresetModel(PresetBrowser& owner) : owner_(owner) {
            }

            int getNumRows() override;
            void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
            void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
            void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
            void returnKeyPressed(int last_row) override;
            void deleteKeyPressed(int last_row) override;
            void selectedRowsChanged(int last_row) override;

        private:
            PresetBrowser& owner_;
        };

        PresetManager& presets_;
        GroupModel group_model_{*this};
        PresetModel preset_model_{*this};
        juce::TextEditor search_;
        juce::ListBox groups_{"Groups", &group_model_};
        juce::ListBox list_{"Presets", &preset_model_};
        IconButton new_group_{icons::Icon::kPlus}, reveal_{icons::Icon::kFolder};
        PillButton import_{"Import..."}, delete_{"Delete"}, rename_{"Rename"}, save_as_{"Save As..."};
        PillButton load_{"Load", true};
        juce::TextEditor entry_;
        Entry entry_mode_{Entry::kNone};
        bool confirm_delete_{false};
        std::unique_ptr<juce::FileChooser> chooser_;

        std::vector<PresetManager::Entry> all_, visible_;
        juce::StringArray group_rows_; // "", favourites marker, then group paths
        int group_row_{0};

        static constexpr auto kAllRow = 0;
        static constexpr auto kFavouritesRow = 1;

        juce::Point<int> getCardSize() const override { return {780, 500}; }

        void layoutContent(juce::Rectangle<int> area) override;

        void paint(juce::Graphics& g) override;

        void opened() override;

        void refresh();

        void applyFilter();

        const PresetManager::Entry* getSelected() const;

        juce::File getTargetGroupDir() const;

        void loadSelected();

        void beginEntry(Entry mode);

        void commitEntry();

        void cancelEntry();

        void updateButtons();

        void timerCallback() override;

        using Sheet::keyPressed;

        bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

        juce::Rectangle<int> footer_status_;
    };
}
