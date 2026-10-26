// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "spectral.hpp"

#include <cmath>
#include <numbers>
#include <pffft.h>

namespace altvane::dialogue::dsp {
    const std::array<float, kFftSize>& vorbisWindow() {
        static const auto window = [] {
            std::array<float, kFftSize> w{};
            constexpr double half = kFftSize / 2.0;
            for (int n = 0; n < kFftSize; ++n) {
                const auto s = std::sin(.5 * std::numbers::pi * (n + .5) / half);
                w[static_cast<size_t>(n)] = static_cast<float>(std::sin(.5 * std::numbers::pi * s * s));
            }
            return w;
        }();
        return window;
    }

    Fft::Fft() :
        setup_(pffft_new_setup(kFftSize, PFFFT_REAL)),
        buffer_(static_cast<float*>(pffft_aligned_malloc(kFftSize * sizeof(float)))),
        work_(static_cast<float*>(pffft_aligned_malloc(kFftSize * sizeof(float)))) {
    }

    Fft::~Fft() {
        pffft_aligned_free(work_);
        pffft_aligned_free(buffer_);
        pffft_destroy_setup(setup_);
    }

    void Fft::forward(const std::span<const float, kFftSize> in, const std::span<Complex, kBins> out) {
        std::copy(in.begin(), in.end(), buffer_);
        pffft_transform_ordered(setup_, buffer_, buffer_, work_, PFFFT_FORWARD);
        // ordered real output: DC, Nyquist, then (re, im) for bins 1 .. N/2 - 1
        out[0] = {buffer_[0], 0.f};
        out[kBins - 1] = {buffer_[1], 0.f};
        for (int k = 1; k < kBins - 1; ++k) {
            out[static_cast<size_t>(k)] = {buffer_[2 * k], buffer_[2 * k + 1]};
        }
    }

    void Fft::inverse(const std::span<const Complex, kBins> in, const std::span<float, kFftSize> out) {
        buffer_[0] = in[0].real();
        buffer_[1] = in[kBins - 1].real();
        for (int k = 1; k < kBins - 1; ++k) {
            buffer_[2 * k] = in[static_cast<size_t>(k)].real();
            buffer_[2 * k + 1] = in[static_cast<size_t>(k)].imag();
        }
        pffft_transform_ordered(setup_, buffer_, buffer_, work_, PFFFT_BACKWARD);
        constexpr float scale = 1.f / static_cast<float>(kFftSize);
        for (int n = 0; n < kFftSize; ++n) {
            out[static_cast<size_t>(n)] = buffer_[n] * scale;
        }
    }
}
