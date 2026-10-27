// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <complex>
#include <span>

struct PFFFT_Setup;

namespace altvane::dialogue::dsp {
    // the separation runs at 48 kHz on 20 ms frames with a 10 ms hop (the models' STFT)
    inline constexpr int kSampleRate = 48000;
    inline constexpr int kFftSize = 960;
    inline constexpr int kHop = 480;
    inline constexpr int kBins = kFftSize / 2 + 1;
    // the models' estimate of a frame arrives this many frames later (their look-ahead)
    inline constexpr int kModelLag = 4;
    // from a sample entering to its processed version leaving, with every hop processed the moment its last sample
    // arrives: a hop of output is complete once the frame kModelLag hops after it is analysed
    inline constexpr int kAlgorithmicLatency = kFftSize + kModelLag * kHop - 1;

    using Complex = std::complex<float>;
    using Spectrum = std::array<Complex, kBins>;

    /**
     * the square root of a power-complementary window: analysis and synthesis with it at 50% overlap
     * reconstruct the input exactly (w[n]^2 + w[n + hop]^2 = 1)
     */
    const std::array<float, kFftSize>& vorbisWindow();

    /**
     * centre frequency of a bin in Hz
     */
    constexpr float binFrequency(const int bin) {
        return static_cast<float>(bin) * static_cast<float>(kSampleRate) / static_cast<float>(kFftSize);
    }

    /**
     * real FFT of kFftSize samples (960 = 2^6 * 3 * 5, so PFFFT rather than a power-of-two FFT)
     * not thread-safe: one per thread
     */
    class Fft {
    public:
        Fft();

        ~Fft();

        Fft(const Fft&) = delete;

        Fft& operator=(const Fft&) = delete;

        /**
         * unnormalised, like numpy.fft.rfft
         */
        void forward(std::span<const float, kFftSize> in, std::span<Complex, kBins> out);

        /**
         * scaled by 1 / kFftSize, so inverse(forward(x)) == x
         */
        void inverse(std::span<const Complex, kBins> in, std::span<float, kFftSize> out);

    private:
        PFFFT_Setup* setup_{nullptr};
        float* buffer_{nullptr};
        float* work_{nullptr};
    };
}
