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
#include "altvane_ui/session/preset_manager.hpp"
#include "../../state/preset_json.hpp"

namespace altvane::eq {
    /**
     * presets stay in the upstream ZL Equalizer JSON format, so they load in either plug-in
     */
    inline PresetCodec presetCodec() {
        return {zlstate::PresetJson::read, zlstate::PresetJson::write};
    }
}
