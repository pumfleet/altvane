// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

/**
 * the compressor editor is built from the shared Altvane design system (libs/ui): bring it into
 * altvane::compressor so the editor can say Knob, Spring, format::... without qualifying every use
 */
namespace altvane::ui {}

namespace altvane::compressor {
    using namespace altvane::ui;
}
