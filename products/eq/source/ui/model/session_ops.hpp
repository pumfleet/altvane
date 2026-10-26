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
#include "engine_bridge.hpp"

namespace altvane::eq::session_ops {
    /**
     * copy bands to the system clipboard, so they can be pasted into another instance
     */
    void copyBands(const EngineBridge& bridge, const std::vector<size_t>& bands);

    /**
     * true if the clipboard holds Altvane EQ bands
     */
    bool canPaste();

    /**
     * paste clipboard bands into free slots as one undo step
     * @return the new band indices
     */
    std::vector<size_t> pasteBands(EngineBridge& bridge);

    /**
     * duplicate bands in place, shifted up by a third of an octave
     */
    std::vector<size_t> duplicateBands(EngineBridge& bridge, const std::vector<size_t>& bands);
}
