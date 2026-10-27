// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace altvane::ui {
    /**
     * fixed-capacity lock-free queue with one producer (the audio thread) and one consumer (the editor)
     * unlike TriBuffer, which keeps only the latest value, every item pushed is delivered in order,
     * so it carries a history (e.g. one column per analysis hop); when full, new items are dropped
     */
    template <typename T, size_t kCapacity>
    class SpscQueue {
    public:
        static_assert(kCapacity >= 2 && (kCapacity & (kCapacity - 1)) == 0, "SpscQueue capacity must be a power of two");
        static_assert(std::atomic<size_t>::is_always_lock_free, "SpscQueue requires lock-free atomic size_t");

        /**
         * producer: false if the queue is full and the item was dropped
         */
        bool push(const T& item) {
            const auto tail = tail_.load(std::memory_order::relaxed);
            if (tail - head_.load(std::memory_order::acquire) >= kCapacity) {
                return false;
            }
            buffer_[tail & kMask] = item;
            tail_.store(tail + 1, std::memory_order::release);
            return true;
        }

        /**
         * consumer: false if the queue is empty
         */
        bool pop(T& item) {
            const auto head = head_.load(std::memory_order::relaxed);
            if (head == tail_.load(std::memory_order::acquire)) {
                return false;
            }
            item = buffer_[head & kMask];
            head_.store(head + 1, std::memory_order::release);
            return true;
        }

        /**
         * consumer: drop everything queued
         */
        void clear() {
            head_.store(tail_.load(std::memory_order::acquire), std::memory_order::release);
        }

        size_t size() const {
            return tail_.load(std::memory_order::acquire) - head_.load(std::memory_order::acquire);
        }

        static constexpr size_t capacity() { return kCapacity; }

    private:
        static constexpr size_t kMask = kCapacity - 1;
        std::array<T, kCapacity> buffer_{};
        alignas(64) std::atomic<size_t> head_{0};
        alignas(64) std::atomic<size_t> tail_{0};
    };
}
