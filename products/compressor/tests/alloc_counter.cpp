// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.
#include "alloc_counter.hpp"

#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

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
#if defined(_WIN32)
        // the MSVC runtime has no aligned_alloc; its aligned blocks must be freed with _aligned_free
        void* p = _aligned_malloc(size == 0 ? 1 : size, a);
#else
        void* p = std::aligned_alloc(a, (size + a - 1) / a * a);
#endif
        if (p != nullptr) {
            return p;
        }
        throw std::bad_alloc();
    }

    void freeAligned(void* p) {
#if defined(_WIN32)
        _aligned_free(p);
#else
        std::free(p);
#endif
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
void operator delete(void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { freeAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { freeAligned(p); }
