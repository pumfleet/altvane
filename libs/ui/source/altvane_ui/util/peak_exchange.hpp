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
#include <juce_audio_basics/juce_audio_basics.h>

namespace altvane::ui {
    /**
     * a stereo peak meter's feed: the audio thread raises each channel's peak, the message thread takes the peaks
     * once a frame and starts them again from silence (lock-free, no allocation)
     */
    class PeakExchange {
    public:
        /**
         * audio thread: raise `channel`'s peak to `magnitude` (linear) if it is louder
         */
        void store(const size_t channel, const float magnitude) {
            auto& peak = peaks_[channel];
            auto current = peak.load(std::memory_order::relaxed);
            while (magnitude > current && !peak.compare_exchange_weak(current, magnitude, std::memory_order::relaxed)) {
            }
        }

        /**
         * message thread: the peaks since the last call, in dB (-120 for silence)
         */
        std::array<float, 2> take() {
            std::array<float, 2> result{};
            for (size_t c = 0; c < 2; ++c) {
                result[c] = juce::Decibels::gainToDecibels(peaks_[c].exchange(0.f, std::memory_order::relaxed), -120.f);
            }
            return result;
        }

    private:
        static_assert(std::atomic<float>::is_always_lock_free, "PeakExchange requires lock-free atomic float");

        std::array<std::atomic<float>, 2> peaks_{};
    };
}
