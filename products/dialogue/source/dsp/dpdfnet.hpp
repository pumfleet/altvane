// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace altvane::dialogue::dsp {
    /**
     * DPDFNet's 48 kHz streaming network (Ceva, the "_hr" ONNX exports), hand-written: weights are read from the
     * ONNX file, the graph itself is this code. It computes what the ONNX graph computes (to float rounding), but
     * runs every stream of a frame together, so stereo reads each weight once, and sends the large products to
     * the platform BLAS
     *
     * one frame in: an unnormalised 960-point STFT frame (481 bins, 480 hop, Vorbis window); out: the speech
     * estimate of the frame four hops earlier
     */
    class DpdfNet {
    public:
        static constexpr int kMaxStreams = 2;
        static constexpr int kBins = 481;

        /**
         * one stream's recurrent state: running normalisations, GRU states and frame buffers
         */
        struct State {
            std::vector<float> erb_mean, spec_mean;
            std::vector<float> erb_feat, df_feat; // 3 frames each, for the first convolutions
            std::vector<float> dprnn; // inter-frame GRU states of every dual-path block
            std::vector<float> enc_gru, erb_gru, df_gru;
            std::vector<float> convp; // 5 frames of the deep-filter pathway input
            std::vector<float> spec, coefs, masked; // 3, 3 and 5 frames: the model's look-ahead
            uint64_t frame{0};
        };

        /**
         * activations for up to kMaxStreams streams; one per thread that runs the network
         */
        struct Scratch {
            Scratch();

            std::vector<float> e0, e1, e2, e3, e3r, tmp, c0, c1, c1r;
            std::vector<float> gx, gh, y, t, gi, hs, h;
            std::vector<float> emb, enc, dec, a, b, gru_in, gru_h, gru_gi, gru_gh;
            std::vector<float> up, x0, mask, df, skip, convp, coefs;
        };

        /**
         * @throw std::runtime_error if the model is not a DPDFNet 48 kHz streaming export
         */
        explicit DpdfNet(std::span<const std::byte> onnx);

        ~DpdfNet();

        DpdfNet(const DpdfNet&) = delete;

        DpdfNet& operator=(const DpdfNet&) = delete;

        int getNumBlocks() const;

        /**
         * a state at the start of a stream (allocates)
         */
        State makeState() const;

        /**
         * back to the start of a stream, without allocating
         */
        void reset(State& state) const;

        /**
         * one frame of `streams` streams (1 .. kMaxStreams); thread-safe for distinct states and scratch,
         * never allocates
         * @param in / out kBins interleaved (re, im) pairs per stream
         */
        void run(int streams, const float* const* in, State* const* states, float* const* out, Scratch& scratch) const;

    private:
        struct Weights;
        std::unique_ptr<Weights> w_;
    };
}
