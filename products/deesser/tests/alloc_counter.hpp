// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <cstddef>

/**
 * counts heap allocations made by the current thread while a Scope is alive (operator new is replaced in
 * alloc_counter.cpp for the whole test binary)
 */
namespace alloc_counter {
    void begin();

    size_t end();

    class Scope {
    public:
        Scope() { begin(); }

        ~Scope() { end(); }

        size_t count() const;
    };
}
