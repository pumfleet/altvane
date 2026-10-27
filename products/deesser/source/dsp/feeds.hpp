// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace altvane::deesser::dsp {
    /**
     * a fixed-size, lock-free single-producer/single-consumer queue: the audio thread pushes, the editor drains;
     * when the editor falls behind, new items are dropped rather than waiting
     */
    template<typename T, size_t N>
    class SpscRing {
        static_assert((N & (N - 1)) == 0, "N must be a power of two");

    public:
        bool push(const T& item) {
            const auto w = write_.load(std::memory_order::relaxed);
            if (w - read_.load(std::memory_order::acquire) >= N) {
                return false;
            }
            items_[w & (N - 1)] = item;
            write_.store(w + 1, std::memory_order::release);
            return true;
        }

        bool pop(T& item) {
            const auto r = read_.load(std::memory_order::relaxed);
            if (r == write_.load(std::memory_order::acquire)) {
                return false;
            }
            item = items_[r & (N - 1)];
            read_.store(r + 1, std::memory_order::release);
            return true;
        }

        /**
         * drop everything queued (consumer side)
         */
        void clear() {
            read_.store(write_.load(std::memory_order::acquire), std::memory_order::release);
        }

    private:
        std::array<T, N> items_{};
        std::atomic<size_t> write_{0}, read_{0};
    };

    /**
     * one column of the level history, 240 per second: peaks over its span, aligned with the (delayed) output
     */
    struct HistoryFrame {
        float input_db{-120.f}, output_db{-120.f}, band_db{-120.f}, reduction_db{0.f};
        // 1 if the detector heard sibilance in its span
        float sibilance{0.f};
    };

    /**
     * one sample for the spectrum: the detection signal, the output (mono) and whether it was sibilance
     */
    struct AnalysisSample {
        float side{0.f}, out{0.f}, sibilant{0.f};
    };

    inline constexpr double kHistoryRate = 240.0;
    inline constexpr size_t kHistoryCapacity = 2048;
    inline constexpr size_t kAnalysisCapacity = 1 << 16;
    inline constexpr size_t kEventCapacity = 256;
}
