// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

/**
 * the EQ editor is built from the shared Altvane design system (libs/ui): bring it into altvane::eq so the
 * editor can say Knob, Spring, format::... without qualifying every use
 */
namespace altvane::ui {}

namespace altvane::eq {
    using namespace altvane::ui;
}
