// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../../PluginProcessor.hpp"
#include "../../dsp/analyzer/fft_analyzer/fft_analyzer_receiver.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_decayer.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_smoother.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_tilter.hpp"

namespace altvane::compressor {
    /**
     * the spectrum of the side chain after its EQ (what the detector hears), from the engine's FFT sender
     * one FFT resolution, ERB-smoothed and tilted like upstream's analyzer; runs on the message thread
     */
    class SideSpectrum final {
    public:
        explicit SideSpectrum(PluginProcessor& p);

        /**
         * start or stop the engine feeding the analyzer
         */
        void setOn(bool on);

        /**
         * pull new audio and recompute; returns true when the spectrum changed
         */
        bool update(float refresh_rate);

        /**
         * dB per FFT bin; bin i is at i * getBinWidth() Hz
         */
        const zldsp::vector::aligned_vector<float>& getSpectrum() const { return spectrum_; }

        double getBinWidth() const { return bin_width_; }

    private:
        PluginProcessor& p_ref_;
        zldsp::analyzer::FFTAnalyzerProcessor processor_;
        zldsp::analyzer::FFTAnalyzerReceiver receiver_{processor_};
        zldsp::analyzer::SpectrumSmoother smoother_;
        zldsp::analyzer::SpectrumTilter tilter_;
        zldsp::analyzer::SpectrumDecayer decayer_;
        // the engine's SIMD routines store to it with aligned stores (SpectrumDecayer::decay)
        zldsp::vector::aligned_vector<float> spectrum_;
        double sample_rate_{0.0}, bin_width_{1.0};
        float refresh_rate_{0.f};
        int history_size_{0};
        bool on_{false};
    };
}
