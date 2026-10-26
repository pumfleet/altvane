// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../widgets/widgets.hpp"
#include "../widgets/sheet.hpp"
#include "../session/ui_settings.hpp"

namespace altvane::ui {
    /**
     * interface and input preferences, the product's own switches, and an about section
     */
    class SettingsSheet final : public Sheet {
    public:
        std::function<void(const UISettings&)> on_change;
        std::function<void()> on_reset_window_size;

        SettingsSheet();

        void setSettings(const UISettings& settings);

        /**
         * add an on / off row for UISettings::switches[key], under its own section after INPUT
         */
        void addSwitch(const juce::String& section, const juce::String& label, const juce::String& key);

        /**
         * the line under the product name in the about section, e.g. what the product is based on
         */
        void setCredit(const juce::String& credit) { credit_ = credit; repaint(); }

        /**
         * a status line shown below the credit (e.g. diagnostics); empty hides it
         */
        void setStatus(const juce::String& status);

        void setCardSize(const juce::Point<int> size) { card_size_ = size; }

    private:
        struct SwitchRow {
            juce::String section, label, key;
            std::unique_ptr<Switch> control;
        };

        UISettings settings_;
        std::array<Chip, UISettings::kScales.size()> scales_;
        std::array<IconButton, 5> accents_{
            IconButton{icons::Icon::kPlus}, IconButton{icons::Icon::kPlus}, IconButton{icons::Icon::kPlus},
            IconButton{icons::Icon::kPlus}, IconButton{icons::Icon::kPlus}
        };
        Switch reduce_motion_;
        std::vector<SwitchRow> switches_;
        ValueField drag_, wheel_;
        PillButton reset_size_{"Reset Window Size"};
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels_;
        std::vector<std::pair<juce::String, int>> sections_;
        juce::Rectangle<int> about_;
        juce::String credit_, status_;
        juce::Point<int> card_size_{560, 500};

        juce::Point<int> getCardSize() const override { return card_size_; }

        void layoutContent(juce::Rectangle<int> area) override;

        void paint(juce::Graphics& g) override;

        void sync();

        void changed();
    };
}
