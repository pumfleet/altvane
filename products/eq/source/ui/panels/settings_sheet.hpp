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
#include "altvane_ui/widgets/sheet.hpp"
#include "../model/ui_settings.hpp"

namespace altvane::eq {
    /**
     * interface, input and analyzer preferences
     */
    class SettingsSheet final : public Sheet {
    public:
        std::function<void(const UISettings&)> on_change;
        std::function<void()> on_reset_window_size;

        SettingsSheet();

        void setSettings(const UISettings& settings);

    private:
        UISettings settings_;
        std::array<Chip, UISettings::kScales.size()> scales_;
        std::array<IconButton, 5> accents_{
            IconButton{icons::Icon::kPlus}, IconButton{icons::Icon::kPlus}, IconButton{icons::Icon::kPlus},
            IconButton{icons::Icon::kPlus}, IconButton{icons::Icon::kPlus}
        };
        Switch reduce_motion_, grab_;
        ValueField drag_, wheel_;
        PillButton reset_size_{"Reset Window Size"};
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels_;
        std::vector<std::pair<juce::String, int>> sections_;
        juce::Rectangle<int> about_;

        juce::Point<int> getCardSize() const override { return {560, 500}; }

        void layoutContent(juce::Rectangle<int> area) override;

        void paint(juce::Graphics& g) override;

        void sync();

        void changed();
    };
}
