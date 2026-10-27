// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "../PluginProcessor.hpp"
#include "../presets.hpp"

namespace altvane::leveler {
    namespace {
        ui::ShellConfig makeConfig() {
            ui::ShellConfig config;
            config.product = "LEVELER";
            config.window = {900, 580, 680, 460};
            config.preset_codec = presetCodec();
            return config;
        }
    }

    Editor::Editor(PluginProcessor& p) : EditorShell(p, p.parameters_, p.session_, makeConfig()) {
        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
    }
}
