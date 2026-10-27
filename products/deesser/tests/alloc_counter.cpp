// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "alloc_counter.hpp"

#include <cstdlib>
#include <new>

namespace {
    thread_local bool counting = false;
    thread_local size_t allocations = 0;

    void* allocate(const std::size_t size) {
        if (counting) {
            ++allocations;
        }
        if (void* p = std::malloc(size == 0 ? 1 : size)) {
            return p;
        }
        throw std::bad_alloc();
    }

    void* allocateAligned(const std::size_t size, const std::align_val_t alignment) {
        if (counting) {
            ++allocations;
        }
        const auto a = static_cast<std::size_t>(alignment);
        if (void* p = std::aligned_alloc(a, (size + a - 1) / a * a)) {
            return p;
        }
        throw std::bad_alloc();
    }
}

namespace alloc_counter {
    void begin() {
        allocations = 0;
        counting = true;
    }

    size_t end() {
        counting = false;
        return allocations;
    }

    size_t Scope::count() const {
        return allocations;
    }
}

void* operator new(const std::size_t size) { return allocate(size); }
void* operator new[](const std::size_t size) { return allocate(size); }
void* operator new(const std::size_t size, const std::align_val_t a) { return allocateAligned(size, a); }
void* operator new[](const std::size_t size, const std::align_val_t a) { return allocateAligned(size, a); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
