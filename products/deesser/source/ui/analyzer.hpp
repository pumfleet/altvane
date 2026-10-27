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
#include <vector>

#include "../dsp/engine.hpp"

struct PFFFT_Setup;

namespace altvane::deesser {
    /**
     * the spectra the band view draws, from the engine's analysis feed (run on the message thread, once a frame):
     * the detection signal (pre), the output (post) and the average spectrum of the esses, whose peak is where
     * this voice's sibilance lives (the sibilance finder)
     */
    class Analyzer {
    public:
        static constexpr int kFFTSize = 4096;
        static constexpr int kHop = kFFTSize / 2;
        static constexpr int kPoints = 240;
        static constexpr float kLowHz = 200.f;
        static constexpr float kHighHz = 22000.f;
        static constexpr float kSilenceDb = -150.f;
        using Curve = std::array<float, kPoints>;

        struct Finder {
            float hz{0.f};
            // 0 until enough esses have been heard, 1 when the estimate has settled
            float confidence{0.f};
        };

        Analyzer();

        ~Analyzer();

        /**
         * drain the engine's analysis feed
         * @return whether a new spectrum was computed
         */
        bool process(dsp::Engine& engine);

        static float pointHz(int point);

        const Curve& getPre() const { return pre_; }

        const Curve& getPost() const { return post_; }

        /**
         * the esses' average spectrum (dB), or all kSilenceDb before any were heard
         */
        const Curve& getSibilance() const { return sibilance_; }

        Finder getFinder() const { return finder_; }

        /**
         * forget the esses heard so far (when the source changes)
         */
        void resetSibilance();

    private:
        double sample_rate_{0.0};
        PFFFT_Setup* setup_{nullptr};
        float* in_{nullptr};
        float* out_{nullptr};
        float* work_{nullptr};
        std::vector<float> window_;
        std::array<std::vector<float>, 2> ring_;
        std::vector<float> flags_;
        size_t write_{0};
        int fresh_{0};
        std::array<std::vector<float>, 2> power_;
        // each point: the bins it spans (or the two around it, interpolated)
        struct Span {
            int first, last;
            float frac;
        };
        std::array<Span, kPoints> spans_{};
        Curve pre_{}, post_{}, sibilance_{};
        std::array<double, kPoints> sibilance_power_{};
        int sibilance_frames_{0};
        Finder finder_;

        void prepare(double sample_rate);
        void computeFrame();
        void spectrum(const std::vector<float>& ring, std::vector<float>& power);
        float pointPower(const std::vector<float>& power, int point) const;
        void updateFinder();
    };
}
