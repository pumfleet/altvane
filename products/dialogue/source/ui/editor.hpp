// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/shell/editor_shell.hpp>

namespace altvane::dialogue {
    class PluginProcessor;

    /**
     * the Altvane Dialogue editor
     */
    class Editor final : public ui::EditorShell {
    public:
        explicit Editor(PluginProcessor& p);

        ~Editor() override;

    private:
        PluginProcessor& p_ref_;
        std::array<ui::Knob, 3> levels_;

        void layout(juce::Rectangle<int> area) override;
        void tick(double dt) override;
    };
}
