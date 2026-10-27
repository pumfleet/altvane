// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "dpdfnet.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "kernels.hpp"
#include "onnx_file.hpp"

// The network, as exported by DPDFNet's onnx_model/dpdfnet_48khz_hr.py (see models/README.md). Activations are
// [position][channel], channels contiguous, and every stream of a frame is stacked into the same matrices, so a
// layer that applies one weight matrix at every position is a single matrix product for the whole frame.

namespace altvane::dialogue::dsp {
    namespace {
        constexpr int kCh = 64; // convolution channels and dual-path GRU width
        constexpr int kErbIn = 480; // the magnitude path's input bins (the last of 481 is dropped)
        constexpr int kErbPos1 = 160, kErbPos2 = 80, kErbPos3 = 40; // after strides 3, 2, 2
        constexpr int kDfBins = 96; // the complex path and the deep filter cover bins below 4.8 kHz
        constexpr int kDfPos = 48; // after stride 2
        constexpr int kMaxPos = kDfPos; // the most positions a dual-path block sees
        constexpr int kEmb = 512, kHidden = 256;
        constexpr int kOrder = 5; // deep-filter taps
        constexpr int kCoefs = kOrder * 2;
        constexpr int kConvpTaps = 5;
        constexpr int kSpecFrames = 3, kMaskedFrames = 5, kCoefFrames = 3, kFeatFrames = 3;
        constexpr float kLayerNormEps = 1e-5f;
        constexpr int kStreams = DpdfNet::kMaxStreams;

        // a frame `age` frames old (0: this one) in a ring of `size` frames
        size_t slot(const uint64_t frame, const int age, const int size) {
            return static_cast<size_t>((frame + static_cast<uint64_t>(size - age)) % static_cast<uint64_t>(size));
        }

        struct Linear {
            std::vector<float> w, b; // w [in][out]
        };

        // gates r, z, n (PyTorch's order), with the reset gate applied after the recurrent product
        struct Gru {
            std::vector<float> wi, bi, wh, bh; // [in][3 hidden], [hidden][3 hidden]
        };

        struct Grouped {
            std::vector<float> w, b; // w [groups][in / groups][out / groups]
            int groups{1}, in{0}, out{0};
        };

        struct Block {
            std::vector<float> intra_wi, intra_bi; // both directions side by side: [64][2 x 192]
            std::array<std::vector<float>, 2> intra_wh, intra_bh;
            Linear fc_intra, fc_inter;
            std::vector<float> ln_intra_g, ln_intra_b, ln_inter_g, ln_inter_b;
            Gru inter;
        };

        struct Scale {
            std::vector<float> w, b; // a per-channel 1x1 convolution
        };

        struct Upsample {
            std::vector<std::vector<float>> dw; // per sub-pixel: [3][64]
            Linear pw;
        };

        void gruCell(const float* gx, const float* gh, float* h, const int hidden) {
            float r[kHidden], z[kHidden], n[kHidden];
            for (int i = 0; i < hidden; ++i) {
                r[i] = gx[i] + gh[i];
                z[i] = gx[hidden + i] + gh[hidden + i];
            }
            kernels::sigmoid(r, hidden);
            kernels::sigmoid(z, hidden);
            for (int i = 0; i < hidden; ++i) {
                n[i] = gx[2 * hidden + i] + r[i] * gh[2 * hidden + i];
            }
            kernels::tanh(n, hidden);
            for (int i = 0; i < hidden; ++i) {
                h[i] = n[i] + z[i] * (h[i] - n[i]);
            }
        }

        void layerNorm(float* x, const int rows, const float* g, const float* b) {
            for (int r = 0; r < rows; ++r) {
                float* v = x + r * kCh;
                float mean = 0.f;
                for (int c = 0; c < kCh; ++c) {
                    mean += v[c];
                }
                mean /= kCh;
                float var = 0.f;
                for (int c = 0; c < kCh; ++c) {
                    var += (v[c] - mean) * (v[c] - mean);
                }
                const float inv = 1.f / std::sqrt(var / kCh + kLayerNormEps);
                for (int c = 0; c < kCh; ++c) {
                    v[c] = (v[c] - mean) * inv * g[c] + b[c];
                }
            }
        }

        void add(float* x, const float* y, const int n) {
            for (int i = 0; i < n; ++i) {
                x[i] += y[i];
            }
        }

        /**
         * depthwise convolution over positions, kernel 3, padding 1: in [positions][64] -> out [out positions][64]
         */
        void depthwise(const float* in, const int positions, const float* w, const int stride, float* out) {
            const int out_positions = (positions - 1) / stride + 1;
            for (int o = 0; o < out_positions; ++o) {
                float acc[kCh] = {};
                for (int k = 0; k < 3; ++k) {
                    const int p = o * stride + k - 1;
                    if (p < 0 || p >= positions) {
                        continue;
                    }
                    const float* x = in + p * kCh;
                    const float* wk = w + k * kCh;
                    for (int c = 0; c < kCh; ++c) {
                        acc[c] += wk[c] * x[c];
                    }
                }
                std::memcpy(out + o * kCh, acc, sizeof(acc));
            }
        }

        void linear(const Linear& l, const int rows, const int in, const int out, const float* x, float* y,
                    const bool relu) {
            kernels::gemm(rows, out, in, x, l.w.data(), l.b.data(), y);
            if (relu) {
                kernels::relu(y, rows * out);
            }
        }

        void grouped(const Grouped& g, const float* x, float* y) {
            const int in = g.in / g.groups, out = g.out / g.groups;
            for (int k = 0; k < g.groups; ++k) {
                float* o = y + k * out;
                std::memcpy(o, g.b.data() + k * out, static_cast<size_t>(out) * sizeof(float));
                const float* w = g.w.data() + static_cast<size_t>(k * in * out);
                for (int i = 0; i < in; ++i) {
                    const float v = x[k * in + i];
                    const float* wi = w + i * out;
                    for (int j = 0; j < out; ++j) {
                        o[j] += v * wi[j];
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- weights

    struct DpdfNet::Weights {
        // features
        float wnorm{}, inv_wnorm{}, alpha{}, one_minus_alpha{}, db_eps{}, db_scale{}, spec_eps{};
        std::vector<float> erb_div, erb_init, spec_init;
        // encoder
        std::vector<float> erb_conv0_w, erb_conv0_b; // [3 time][3 freq][64]
        std::array<std::vector<float>, 3> erb_dw;
        std::array<Linear, 3> erb_pw;
        std::vector<float> df_conv0_w; // [3][3][64]; channels 0-31 read the real part, 32-63 the imaginary
        Linear df_conv0_pw;
        std::vector<float> df_dw;
        Linear df_pw;
        std::vector<Block> erb_blocks, df_blocks;
        Grouped erb_emb, df_emb, enc_in, enc_out;
        Gru enc_gru;
        // magnitude-mask decoder
        Grouped dec_in, dec_out, dec_fc;
        std::array<Gru, 2> dec_gru;
        std::array<Scale, 4> pathway; // conv3p, conv2p, conv1p, conv0p
        std::array<Upsample, 3> up; // convt3, convt2, convt1
        std::vector<float> mask_w; // [3][64]
        float mask_b{};
        // deep-filter decoder
        Grouped df_in, df_skip, df_out;
        std::array<Gru, 2> df_gru;
        std::vector<float> convp_w; // [5 time][64][10], block-diagonal over the two channel groups
        Linear convp_pw;

        explicit Weights(const OnnxFile& f);
    };

    namespace {
        const std::vector<float>& tensor(const OnnxFile& f, const std::string& node, const size_t input,
                                         const std::vector<int64_t>& dims) {
            const auto& t = f.input("/model/" + node, input);
            if (t.dims != dims) {
                throw std::runtime_error("unexpected weight shape for " + node);
            }
            return t.data;
        }

        float scalar(const OnnxFile& f, const std::string& node, const size_t input) {
            const auto& t = f.input(node, input);
            if (t.data.size() != 1) {
                throw std::runtime_error("expected a scalar at " + node);
            }
            return t.data[0];
        }

        // [rows][cols] -> [cols][rows]
        std::vector<float> transpose(const std::vector<float>& m, const int rows, const int cols) {
            std::vector<float> t(m.size());
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    t[static_cast<size_t>(c * rows + r)] = m[static_cast<size_t>(r * cols + c)];
                }
            }
            return t;
        }

        // a Conv (out, in, 1, 1) or Gemm (out, in) weight, as [in][out]
        Linear pointwise(const OnnxFile& f, const std::string& node, const int in, const int out, const bool conv = true) {
            Linear l;
            const auto dims = conv ? std::vector<int64_t>{out, in, 1, 1} : std::vector<int64_t>{out, in};
            l.w = transpose(tensor(f, node, 1, dims), out, in);
            l.b = tensor(f, node, 2, {out});
            return l;
        }

        // a depthwise (64, 1, 1, 3) kernel as [3][64]
        std::vector<float> depthwiseKernel(const OnnxFile& f, const std::string& node) {
            return transpose(tensor(f, node, 1, {kCh, 1, 1, 3}), kCh, 3);
        }

        Grouped groupedLinear(const OnnxFile& f, const std::string& prefix, const int groups, const int in, const int out) {
            Grouped g;
            g.groups = groups;
            g.in = in;
            g.out = out;
            g.w = tensor(f, prefix + "MatMul", 1, {groups, in / groups, out / groups});
            g.b = tensor(f, prefix + "Add", 1, {out});
            return g;
        }

        // a GRUCell exported as two Gemms
        Gru gruCellWeights(const OnnxFile& f, const std::string& prefix, const int in, const int hidden) {
            Gru g;
            g.wi = transpose(tensor(f, prefix + "Gemm", 1, {3 * hidden, in}), 3 * hidden, in);
            g.bi = tensor(f, prefix + "Gemm", 2, {3 * hidden});
            g.wh = transpose(tensor(f, prefix + "Gemm_1", 1, {3 * hidden, hidden}), 3 * hidden, hidden);
            g.bh = tensor(f, prefix + "Gemm_1", 2, {3 * hidden});
            return g;
        }

        // ONNX's GRU orders the gates z, r, h; ours r, z, n
        std::vector<float> onnxGatesToOurs(const float* g, const int hidden, const int cols) {
            std::vector<float> out(static_cast<size_t>(3 * hidden * cols));
            const auto block = static_cast<size_t>(hidden * cols);
            std::memcpy(out.data(), g + block, block * sizeof(float)); // r
            std::memcpy(out.data() + block, g, block * sizeof(float)); // z
            std::memcpy(out.data() + 2 * block, g + 2 * block, block * sizeof(float)); // n
            return out;
        }

        Block dprnnBlock(const OnnxFile& f, const std::string& prefix) {
            Block b;
            const auto gru = prefix + "intra_gru/GRU";
            const auto& w = tensor(f, gru, 1, {2, 3 * kCh, kCh});
            const auto& r = tensor(f, gru, 2, {2, 3 * kCh, kCh});
            const auto& bias = tensor(f, gru, 3, {2, 6 * kCh});
            b.intra_wi.assign(static_cast<size_t>(kCh * 6 * kCh), 0.f);
            b.intra_bi.resize(6 * kCh);
            for (int d = 0; d < 2; ++d) {
                const auto wd = transpose(onnxGatesToOurs(w.data() + d * 3 * kCh * kCh, kCh, kCh), 3 * kCh, kCh);
                for (int i = 0; i < kCh; ++i) {
                    std::memcpy(b.intra_wi.data() + i * 6 * kCh + d * 3 * kCh, wd.data() + i * 3 * kCh,
                                3 * kCh * sizeof(float));
                }
                b.intra_wh[static_cast<size_t>(d)] = transpose(onnxGatesToOurs(r.data() + d * 3 * kCh * kCh, kCh, kCh),
                                                               3 * kCh, kCh);
                const auto bw = onnxGatesToOurs(bias.data() + d * 6 * kCh, kCh, 1);
                std::copy(bw.begin(), bw.end(), b.intra_bi.begin() + d * 3 * kCh);
                b.intra_bh[static_cast<size_t>(d)] = onnxGatesToOurs(bias.data() + d * 6 * kCh + 3 * kCh, kCh, 1);
            }
            b.fc_intra.w = tensor(f, prefix + "fc_intra/MatMul", 1, {2 * kCh, kCh}); // already [in][out]
            b.fc_intra.b = tensor(f, prefix + "fc_intra/Add", 0, {kCh});
            b.ln_intra_g = tensor(f, prefix + "ln_intra/LayerNormalization", 1, {kCh});
            b.ln_intra_b = tensor(f, prefix + "ln_intra/LayerNormalization", 2, {kCh});
            b.inter = gruCellWeights(f, prefix + "inter_gru/grucell/", kCh, kCh);
            b.fc_inter = pointwise(f, prefix + "fc_inter/Gemm", kCh, kCh, false);
            b.ln_inter_g = tensor(f, prefix + "ln_inter/LayerNormalization", 1, {kCh});
            b.ln_inter_b = tensor(f, prefix + "ln_inter/LayerNormalization", 2, {kCh});
            return b;
        }

        // a full (out, in, 3 time, 3 freq) first-layer kernel as [3][3][out]
        void firstConv(const std::vector<float>& w, const int out, const int channel_offset, std::vector<float>& dest) {
            for (int c = 0; c < out; ++c) {
                for (int t = 0; t < 3; ++t) {
                    for (int k = 0; k < 3; ++k) {
                        dest[static_cast<size_t>((t * 3 + k) * kCh + channel_offset + c)] = w[static_cast<size_t>(c * 9 + t * 3 + k)];
                    }
                }
            }
        }

        std::vector<float> parseFloats(const std::string& text) {
            std::vector<float> values;
            size_t pos = 0;
            while (pos < text.size()) {
                size_t used = 0;
                values.push_back(std::stof(text.substr(pos), &used));
                pos += used;
                while (pos < text.size() && (text[pos] == ',' || text[pos] == ' ')) {
                    ++pos;
                }
            }
            return values;
        }
    }

    DpdfNet::Weights::Weights(const OnnxFile& f) {
        if (std::stoi(f.metadata("n_fft")) != 960 || std::stoi(f.metadata("hop_length")) != 480
            || std::stoi(f.metadata("freq_bins")) != kBins || std::stoi(f.metadata("spec_bins")) != kDfBins) {
            throw std::runtime_error("unexpected model layout");
        }
        erb_init = parseFloats(f.metadata("erb_norm_init"));
        spec_init = parseFloats(f.metadata("spec_norm_init"));
        if (erb_init.size() != kBins || spec_init.size() != kDfBins) {
            throw std::runtime_error("unexpected normalisation state in the model metadata");
        }

        wnorm = scalar(f, "/Mul", 1);
        inv_wnorm = scalar(f, "/Mul_1", 1);
        alpha = scalar(f, "/model/erb_norm/Mul", 1);
        one_minus_alpha = scalar(f, "/model/erb_norm/Mul_1", 1);
        db_eps = scalar(f, "/model/Add", 1);
        db_scale = scalar(f, "/model/Mul", 1) / scalar(f, "/model/Div", 1); // 10 log10(x) = ln(x) / ln(10) * 10
        spec_eps = scalar(f, "/model/spec_norm/Add_1", 1);
        erb_div = tensor(f, "erb_norm/Div", 1, {1, 1, kBins});

        erb_conv0_w.resize(9 * kCh);
        firstConv(tensor(f, "enc/erb_conv0/erb_conv0.1/Conv", 1, {kCh, 1, 3, 3}), kCh, 0, erb_conv0_w);
        erb_conv0_b = tensor(f, "enc/erb_conv0/erb_conv0.1/Conv", 2, {kCh});
        for (int i = 0; i < 3; ++i) {
            const auto name = "enc/erb_conv" + std::to_string(i + 1) + "/erb_conv" + std::to_string(i + 1);
            erb_dw[static_cast<size_t>(i)] = depthwiseKernel(f, name + ".0/Conv");
            erb_pw[static_cast<size_t>(i)] = pointwise(f, name + ".1/Conv", kCh, kCh);
        }
        df_conv0_w.resize(9 * kCh);
        for (int g = 0; g < 2; ++g) {
            const auto name = "enc/df_conv0/df_conv0.1/convs." + std::to_string(g) + "/Conv";
            firstConv(tensor(f, name, 1, {kCh / 2, 1, 3, 3}), kCh / 2, g * kCh / 2, df_conv0_w);
        }
        df_conv0_pw = pointwise(f, "enc/df_conv0/df_conv0.2/Conv", kCh, kCh);
        df_dw = depthwiseKernel(f, "enc/df_conv1/df_conv1.0/Conv");
        df_pw = pointwise(f, "enc/df_conv1/df_conv1.1/Conv", kCh, kCh);

        for (int b = 0; f.hasNode("/model/enc/dprnn_erb/blocks." + std::to_string(b) + "/intra_gru/GRU"); ++b) {
            erb_blocks.push_back(dprnnBlock(f, "enc/dprnn_erb/blocks." + std::to_string(b) + "/"));
            df_blocks.push_back(dprnnBlock(f, "enc/dprnn_df/blocks." + std::to_string(b) + "/"));
        }
        if (erb_blocks.empty()) {
            throw std::runtime_error("the model has no dual-path blocks");
        }
        erb_emb = groupedLinear(f, "enc/erb_fc_emb/erb_fc_emb.0/", 32, kErbPos3 * kCh, kEmb);
        df_emb = groupedLinear(f, "enc/df_fc_emb/df_fc_emb.0/", 32, kDfPos * kCh, kEmb);
        enc_in = groupedLinear(f, "enc/emb_gru/linear_in/linear_in.0/", 16, 2 * kEmb, kHidden);
        enc_gru = gruCellWeights(f, "enc/emb_gru/gru.0/grucell/", kHidden, kHidden);
        enc_out = groupedLinear(f, "enc/emb_gru/linear_out/linear_out.0/", 16, kHidden, kEmb);

        dec_in = groupedLinear(f, "erb_dec/emb_gru/linear_in/linear_in.0/", 16, kEmb, kHidden);
        for (int l = 0; l < 2; ++l) {
            dec_gru[static_cast<size_t>(l)] = gruCellWeights(f, "erb_dec/emb_gru/gru." + std::to_string(l) + "/grucell/",
                                                             kHidden, kHidden);
            df_gru[static_cast<size_t>(l)] = gruCellWeights(f, "df_dec/df_gru/gru." + std::to_string(l) + "/grucell/",
                                                            kHidden, kHidden);
        }
        dec_out = groupedLinear(f, "erb_dec/emb_gru/linear_out/linear_out.0/", 16, kHidden, kEmb);
        dec_fc = groupedLinear(f, "erb_dec/erb_fc_emb/erb_fc_emb.0/", 32, kEmb, kErbPos3 * kCh);
        const std::array<const char*, 4> pathway_names{"conv3p", "conv2p", "conv1p", "conv0p"};
        for (size_t i = 0; i < 4; ++i) {
            const auto name = std::string("erb_dec/") + pathway_names[i] + "/" + pathway_names[i] + ".0/Conv";
            pathway[i].w = tensor(f, name, 1, {kCh, 1, 1, 1});
            pathway[i].b = tensor(f, name, 2, {kCh});
        }
        const std::array<const char*, 3> up_names{"convt3", "convt2", "convt1"};
        const std::array<int, 3> up_factors{2, 2, 3};
        for (size_t i = 0; i < 3; ++i) {
            const auto name = std::string("erb_dec/") + up_names[i] + "/" + up_names[i];
            for (int s = 0; s < up_factors[i]; ++s) {
                up[i].dw.push_back(depthwiseKernel(f, name + ".0/convs." + std::to_string(s) + "/Conv"));
            }
            up[i].pw = pointwise(f, name + ".1/Conv", kCh, kCh);
        }
        mask_w = transpose(tensor(f, "erb_dec/conv0_out/conv0_out.0/Conv", 1, {1, kCh, 1, 3}), kCh, 3);
        mask_b = tensor(f, "erb_dec/conv0_out/conv0_out.0/Conv", 2, {1})[0];

        df_in = groupedLinear(f, "df_dec/df_gru/linear_in/linear_in.0/", 8, kEmb, kHidden);
        df_skip = groupedLinear(f, "df_dec/df_skip/", 16, kEmb, kHidden);
        df_out = groupedLinear(f, "df_dec/df_out/df_out.0/", 16, kHidden, kDfBins * kCoefs);
        convp_w.assign(static_cast<size_t>(kConvpTaps * kCh * kCoefs), 0.f);
        for (int g = 0; g < 2; ++g) {
            const auto& w = tensor(f, "df_dec/df_convp/df_convp.1/convs." + std::to_string(g) + "/Conv", 1,
                                   {kOrder, kCh / 2, kConvpTaps, 1});
            for (int j = 0; j < kOrder; ++j) {
                for (int ci = 0; ci < kCh / 2; ++ci) {
                    for (int t = 0; t < kConvpTaps; ++t) {
                        convp_w[static_cast<size_t>((t * kCh + g * kCh / 2 + ci) * kCoefs + g * kOrder + j)] =
                            w[static_cast<size_t>((j * kCh / 2 + ci) * kConvpTaps + t)];
                    }
                }
            }
        }
        convp_pw = pointwise(f, "df_dec/df_convp/df_convp.2/Conv", kCoefs, kCoefs);

        // the state this code keeps must be the state the export declares, or the layout is not the one ported here
        const auto blocks = static_cast<int64_t>(erb_blocks.size());
        const auto ours = kBins + kDfBins + kFeatFrames * kBins + blocks * (kErbPos3 + kDfPos) * kCh
                          + kFeatFrames * 2 * kDfBins + 5 * kHidden + kConvpTaps * kDfBins * kCh
                          + kSpecFrames * kBins * 2 + kCoefFrames * kDfBins * kCoefs + kMaskedFrames * kBins * 2;
        if (std::stoll(f.metadata("state_size")) != ours) {
            throw std::runtime_error("unexpected model state size");
        }
    }

    // ---------------------------------------------------------------- state and scratch

    DpdfNet::Scratch::Scratch() {
        auto per_stream = [](std::vector<float>& v, const int n) { v.assign(static_cast<size_t>(kStreams * n), 0.f); };
        per_stream(e0, kErbIn * kCh);
        per_stream(e1, kErbPos1 * kCh);
        per_stream(e2, kErbPos2 * kCh);
        per_stream(e3, kErbPos3 * kCh);
        per_stream(e3r, kErbPos3 * kCh);
        per_stream(tmp, kErbIn * kCh);
        per_stream(c0, kDfBins * kCh);
        per_stream(c1, kDfPos * kCh);
        per_stream(c1r, kDfPos * kCh);
        per_stream(gx, kMaxPos * 6 * kCh);
        per_stream(gh, kMaxPos * 3 * kCh);
        per_stream(y, kMaxPos * 2 * kCh);
        per_stream(t, kMaxPos * kCh);
        per_stream(gi, kMaxPos * 3 * kCh);
        per_stream(hs, kMaxPos * kCh);
        per_stream(h, kCh);
        per_stream(emb, 2 * kEmb);
        per_stream(enc, kEmb);
        per_stream(dec, kErbPos3 * kCh);
        per_stream(a, kHidden);
        per_stream(b, kEmb);
        per_stream(gru_in, kHidden);
        per_stream(gru_h, kHidden);
        per_stream(gru_gi, 3 * kHidden);
        per_stream(gru_gh, 3 * kHidden);
        per_stream(up, kErbIn * kCh);
        per_stream(x0, kErbIn * kCh);
        per_stream(mask, kBins);
        per_stream(df, kHidden);
        per_stream(skip, kHidden);
        per_stream(convp, kDfBins * kCoefs);
        per_stream(coefs, kDfBins * kCoefs);
    }

    DpdfNet::DpdfNet(const std::span<const std::byte> onnx) :
        w_(std::make_unique<Weights>(OnnxFile(onnx))) {
    }

    DpdfNet::~DpdfNet() = default;

    int DpdfNet::getNumBlocks() const {
        return static_cast<int>(w_->erb_blocks.size());
    }

    DpdfNet::State DpdfNet::makeState() const {
        State s;
        const auto blocks = static_cast<size_t>(getNumBlocks());
        s.erb_mean.resize(kBins);
        s.spec_mean.resize(kDfBins);
        s.erb_feat.resize(kFeatFrames * kBins);
        s.df_feat.resize(kFeatFrames * 2 * kDfBins);
        s.dprnn.resize(blocks * (kErbPos3 + kDfPos) * kCh);
        s.enc_gru.resize(kHidden);
        s.erb_gru.resize(2 * kHidden);
        s.df_gru.resize(2 * kHidden);
        s.convp.resize(kConvpTaps * kDfBins * kCh);
        s.spec.resize(kSpecFrames * kBins * 2);
        s.coefs.resize(kCoefFrames * kDfBins * kCoefs);
        s.masked.resize(kMaskedFrames * kBins * 2);
        reset(s);
        return s;
    }

    void DpdfNet::reset(State& s) const {
        for (auto* v : {&s.erb_feat, &s.df_feat, &s.dprnn, &s.enc_gru, &s.erb_gru, &s.df_gru, &s.convp, &s.spec,
                        &s.coefs, &s.masked}) {
            std::fill(v->begin(), v->end(), 0.f);
        }
        std::copy(w_->erb_init.begin(), w_->erb_init.end(), s.erb_mean.begin());
        std::copy(w_->spec_init.begin(), w_->spec_init.end(), s.spec_mean.begin());
        s.frame = 0;
    }

    // ---------------------------------------------------------------- inference

    namespace {
        /**
         * one dual-path block over `streams` x `positions` rows of x (in place)
         * @param hidden each stream's inter-frame GRU state, [positions][64]
         */
        void runBlock(const Block& b, const int streams, const int positions, float* x, float* const* hidden,
                      DpdfNet::Scratch& s) {
            const int rows = streams * positions;
            // across frequency: a bidirectional GRU, its two directions' input products in one matrix product
            kernels::gemm(rows, 6 * kCh, kCh, x, b.intra_wi.data(), b.intra_bi.data(), s.gx.data());
            for (int d = 0; d < 2; ++d) {
                std::fill(s.h.begin(), s.h.begin() + streams * kCh, 0.f);
                for (int step = 0; step < positions; ++step) {
                    const int p = d == 0 ? step : positions - 1 - step;
                    kernels::gemm(streams, 3 * kCh, kCh, s.h.data(), b.intra_wh[static_cast<size_t>(d)].data(),
                                  b.intra_bh[static_cast<size_t>(d)].data(), s.gh.data());
                    for (int k = 0; k < streams; ++k) {
                        const int row = k * positions + p;
                        float* h = s.h.data() + k * kCh;
                        gruCell(s.gx.data() + row * 6 * kCh + d * 3 * kCh, s.gh.data() + k * 3 * kCh, h, kCh);
                        std::memcpy(s.y.data() + row * 2 * kCh + d * kCh, h, kCh * sizeof(float));
                    }
                }
            }
            linear(b.fc_intra, rows, 2 * kCh, kCh, s.y.data(), s.t.data(), false);
            layerNorm(s.t.data(), rows, b.ln_intra_g.data(), b.ln_intra_b.data());
            add(x, s.t.data(), rows * kCh);

            // across time: a GRU cell at every position
            kernels::gemm(rows, 3 * kCh, kCh, x, b.inter.wi.data(), b.inter.bi.data(), s.gi.data());
            for (int k = 0; k < streams; ++k) {
                std::memcpy(s.hs.data() + k * positions * kCh, hidden[k], static_cast<size_t>(positions * kCh) * sizeof(float));
            }
            kernels::gemm(rows, 3 * kCh, kCh, s.hs.data(), b.inter.wh.data(), b.inter.bh.data(), s.gh.data());
            for (int r = 0; r < rows; ++r) {
                gruCell(s.gi.data() + r * 3 * kCh, s.gh.data() + r * 3 * kCh, s.hs.data() + r * kCh, kCh);
            }
            for (int k = 0; k < streams; ++k) {
                std::memcpy(hidden[k], s.hs.data() + k * positions * kCh, static_cast<size_t>(positions * kCh) * sizeof(float));
            }
            linear(b.fc_inter, rows, kCh, kCh, s.hs.data(), s.t.data(), false);
            layerNorm(s.t.data(), rows, b.ln_inter_g.data(), b.ln_inter_b.data());
            add(x, s.t.data(), rows * kCh);
        }

        /**
         * a GRU cell over one row per stream
         * @param x [streams][256] in; out [streams][256] receives the new states
         * @param states each stream's state (updated)
         */
        void runGru(const Gru& g, const int streams, const float* x, float* const* states, float* out,
                    DpdfNet::Scratch& s) {
            kernels::gemm(streams, 3 * kHidden, kHidden, x, g.wi.data(), g.bi.data(), s.gru_gi.data());
            for (int k = 0; k < streams; ++k) {
                std::memcpy(s.gru_h.data() + k * kHidden, states[k], kHidden * sizeof(float));
            }
            kernels::gemm(streams, 3 * kHidden, kHidden, s.gru_h.data(), g.wh.data(), g.bh.data(), s.gru_gh.data());
            for (int k = 0; k < streams; ++k) {
                gruCell(s.gru_gi.data() + k * 3 * kHidden, s.gru_gh.data() + k * 3 * kHidden, states[k], kHidden);
                std::memcpy(out + k * kHidden, states[k], kHidden * sizeof(float));
            }
        }

        /**
         * x [streams][positions][64] -> out [streams][positions x factor][64], each sub-pixel its own depthwise kernel
         */
        void upsample(const Upsample& u, const int streams, const int positions, const float* x, float* out,
                      DpdfNet::Scratch& s) {
            const int factor = static_cast<int>(u.dw.size());
            const int out_positions = positions * factor;
            for (int k = 0; k < streams; ++k) {
                const float* xin = x + k * positions * kCh;
                float* t = s.tmp.data() + k * out_positions * kCh;
                for (int sub = 0; sub < factor; ++sub) {
                    const float* w = u.dw[static_cast<size_t>(sub)].data();
                    for (int o = 0; o < positions; ++o) {
                        float acc[kCh] = {};
                        for (int j = 0; j < 3; ++j) {
                            const int p = o + j - 1;
                            if (p < 0 || p >= positions) {
                                continue;
                            }
                            for (int c = 0; c < kCh; ++c) {
                                acc[c] += w[j * kCh + c] * xin[p * kCh + c];
                            }
                        }
                        std::memcpy(t + (o * factor + sub) * kCh, acc, sizeof(acc));
                    }
                }
            }
            linear(u.pw, streams * out_positions, kCh, kCh, s.tmp.data(), out, true);
        }

        /**
         * out = relu(x * scale + bias) + skip, per channel
         */
        void pathway(const Scale& p, const int rows, const float* x, const float* skip, float* out) {
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < kCh; ++c) {
                    out[r * kCh + c] = std::max(x[r * kCh + c] * p.w[static_cast<size_t>(c)] + p.b[static_cast<size_t>(c)], 0.f)
                                       + skip[r * kCh + c];
                }
            }
        }
    }

    void DpdfNet::run(const int streams, const float* const* in, State* const* states, float* const* out,
                      Scratch& s) const {
        const auto& w = *w_;
        const int n = std::clamp(streams, 1, kStreams);

        // ---- features: the magnitude in dB and the low band of the spectrum, each with a running normalisation
        for (int k = 0; k < n; ++k) {
            auto& st = *states[k];
            float* spec = st.spec.data() + slot(st.frame, 0, kSpecFrames) * kBins * 2;
            float* erb = st.erb_feat.data() + slot(st.frame, 0, kFeatFrames) * kBins;
            float* df = st.df_feat.data() + slot(st.frame, 0, kFeatFrames) * 2 * kDfBins;
            for (int f = 0; f < kBins; ++f) {
                const float re = in[k][2 * f] * w.wnorm, im = in[k][2 * f + 1] * w.wnorm;
                spec[2 * f] = re;
                spec[2 * f + 1] = im;
                const float mag = std::sqrt(re * re + im * im);
                const float db = std::log(mag + w.db_eps) * w.db_scale;
                st.erb_mean[static_cast<size_t>(f)] = w.alpha * st.erb_mean[static_cast<size_t>(f)] + w.one_minus_alpha * db;
                erb[f] = (db - st.erb_mean[static_cast<size_t>(f)]) / w.erb_div[static_cast<size_t>(f)];
                if (f < kDfBins) {
                    auto& m = st.spec_mean[static_cast<size_t>(f)];
                    m = w.alpha * m + w.one_minus_alpha * mag;
                    const float d = std::sqrt(m + w.spec_eps);
                    df[f] = re / d;
                    df[kDfBins + f] = im / d;
                }
            }
        }

        // ---- encoder: magnitude path, 480 -> 160 -> 80 -> 40 positions
        for (int k = 0; k < n; ++k) {
            const auto& st = *states[k];
            float* e0 = s.e0.data() + k * kErbIn * kCh;
            for (int f = 0; f < kErbIn; ++f) {
                float acc[kCh];
                std::memcpy(acc, w.erb_conv0_b.data(), sizeof(acc));
                for (int t = 0; t < 3; ++t) {
                    const float* row = st.erb_feat.data() + slot(st.frame, 2 - t, kFeatFrames) * kBins;
                    for (int j = 0; j < 3; ++j) {
                        const int i = f + j - 1;
                        if (i < 0 || i >= kErbIn) {
                            continue;
                        }
                        const float v = row[i];
                        const float* wk = w.erb_conv0_w.data() + (t * 3 + j) * kCh;
                        for (int c = 0; c < kCh; ++c) {
                            acc[c] += v * wk[c];
                        }
                    }
                }
                for (int c = 0; c < kCh; ++c) {
                    e0[f * kCh + c] = std::max(acc[c], 0.f);
                }
            }
        }
        const std::array<float*, 4> erb_levels{s.e0.data(), s.e1.data(), s.e2.data(), s.e3.data()};
        const std::array<int, 4> erb_positions{kErbIn, kErbPos1, kErbPos2, kErbPos3};
        for (size_t l = 0; l < 3; ++l) {
            const int stride = l == 0 ? 3 : 2;
            for (int k = 0; k < n; ++k) {
                depthwise(erb_levels[l] + k * erb_positions[l] * kCh, erb_positions[l], w.erb_dw[l].data(), stride,
                          s.tmp.data() + k * erb_positions[l + 1] * kCh);
            }
            linear(w.erb_pw[l], n * erb_positions[l + 1], kCh, kCh, s.tmp.data(), erb_levels[l + 1], true);
        }
        std::memcpy(s.e3r.data(), s.e3.data(), static_cast<size_t>(n * kErbPos3 * kCh) * sizeof(float));
        const auto blocks = w.erb_blocks.size();
        for (size_t b = 0; b < blocks; ++b) {
            std::array<float*, kStreams> hidden{};
            for (int k = 0; k < n; ++k) {
                hidden[static_cast<size_t>(k)] = states[k]->dprnn.data() + b * kErbPos3 * kCh;
            }
            runBlock(w.erb_blocks[b], n, kErbPos3, s.e3r.data(), hidden.data(), s);
        }

        // ---- encoder: complex path, 96 -> 48 positions
        for (int k = 0; k < n; ++k) {
            const auto& st = *states[k];
            float* c0 = s.tmp.data() + k * kDfBins * kCh;
            for (int f = 0; f < kDfBins; ++f) {
                float acc[kCh] = {};
                for (int t = 0; t < 3; ++t) {
                    const float* row = st.df_feat.data() + slot(st.frame, 2 - t, kFeatFrames) * 2 * kDfBins;
                    for (int j = 0; j < 3; ++j) {
                        const int i = f + j - 1;
                        if (i < 0 || i >= kDfBins) {
                            continue;
                        }
                        const float re = row[i], im = row[kDfBins + i];
                        const float* wk = w.df_conv0_w.data() + (t * 3 + j) * kCh;
                        for (int c = 0; c < kCh / 2; ++c) {
                            acc[c] += re * wk[c];
                        }
                        for (int c = kCh / 2; c < kCh; ++c) {
                            acc[c] += im * wk[c];
                        }
                    }
                }
                std::memcpy(c0 + f * kCh, acc, sizeof(acc));
            }
        }
        linear(w.df_conv0_pw, n * kDfBins, kCh, kCh, s.tmp.data(), s.c0.data(), true);
        for (int k = 0; k < n; ++k) {
            auto& st = *states[k];
            std::memcpy(st.convp.data() + slot(st.frame, 0, kConvpTaps) * kDfBins * kCh, s.c0.data() + k * kDfBins * kCh,
                        kDfBins * kCh * sizeof(float));
            depthwise(s.c0.data() + k * kDfBins * kCh, kDfBins, w.df_dw.data(), 2, s.tmp.data() + k * kDfPos * kCh);
        }
        linear(w.df_pw, n * kDfPos, kCh, kCh, s.tmp.data(), s.c1r.data(), true);
        for (size_t b = 0; b < blocks; ++b) {
            std::array<float*, kStreams> hidden{};
            for (int k = 0; k < n; ++k) {
                hidden[static_cast<size_t>(k)] = states[k]->dprnn.data() + blocks * kErbPos3 * kCh + b * kDfPos * kCh;
            }
            runBlock(w.df_blocks[b], n, kDfPos, s.c1r.data(), hidden.data(), s);
        }

        // ---- embedding
        std::array<float*, kStreams> gru_states{};
        for (int k = 0; k < n; ++k) {
            float* emb = s.emb.data() + k * 2 * kEmb;
            grouped(w.erb_emb, s.e3r.data() + k * kErbPos3 * kCh, emb);
            grouped(w.df_emb, s.c1r.data() + k * kDfPos * kCh, emb + kEmb);
            kernels::relu(emb, 2 * kEmb);
            grouped(w.enc_in, emb, s.a.data() + k * kHidden);
            gru_states[static_cast<size_t>(k)] = states[k]->enc_gru.data();
        }
        kernels::relu(s.a.data(), n * kHidden);
        runGru(w.enc_gru, n, s.a.data(), gru_states.data(), s.gru_in.data(), s);
        for (int k = 0; k < n; ++k) {
            grouped(w.enc_out, s.gru_in.data() + k * kHidden, s.enc.data() + k * kEmb);
        }
        kernels::relu(s.enc.data(), n * kEmb);

        // ---- magnitude-mask decoder: 40 -> 80 -> 160 -> 480 positions, with the encoder's levels added back
        for (int k = 0; k < n; ++k) {
            grouped(w.dec_in, s.enc.data() + k * kEmb, s.a.data() + k * kHidden);
        }
        kernels::relu(s.a.data(), n * kHidden);
        const float* gru_x = s.a.data();
        for (size_t l = 0; l < 2; ++l) {
            for (int k = 0; k < n; ++k) {
                gru_states[static_cast<size_t>(k)] = states[k]->erb_gru.data() + l * kHidden;
            }
            runGru(w.dec_gru[l], n, gru_x, gru_states.data(), s.gru_in.data(), s);
            gru_x = s.gru_in.data();
        }
        for (int k = 0; k < n; ++k) {
            grouped(w.dec_out, s.gru_in.data() + k * kHidden, s.b.data() + k * kEmb);
        }
        kernels::relu(s.b.data(), n * kEmb);
        for (int k = 0; k < n; ++k) {
            grouped(w.dec_fc, s.b.data() + k * kEmb, s.dec.data() + k * kErbPos3 * kCh);
        }
        kernels::relu(s.dec.data(), n * kErbPos3 * kCh);

        pathway(w.pathway[0], n * kErbPos3, s.e3.data(), s.dec.data(), s.x0.data());
        upsample(w.up[0], n, kErbPos3, s.x0.data(), s.up.data(), s);
        pathway(w.pathway[1], n * kErbPos2, s.e2.data(), s.up.data(), s.x0.data());
        upsample(w.up[1], n, kErbPos2, s.x0.data(), s.up.data(), s);
        pathway(w.pathway[2], n * kErbPos1, s.e1.data(), s.up.data(), s.x0.data());
        upsample(w.up[2], n, kErbPos1, s.x0.data(), s.up.data(), s);
        pathway(w.pathway[3], n * kErbIn, s.e0.data(), s.up.data(), s.x0.data());
        for (int k = 0; k < n; ++k) {
            const float* x0 = s.x0.data() + k * kErbIn * kCh;
            float* mask = s.mask.data() + k * kBins;
            for (int f = 0; f < kErbIn; ++f) {
                float acc[kCh] = {};
                for (int j = 0; j < 3; ++j) {
                    const int p = f + j - 1;
                    if (p < 0 || p >= kErbIn) {
                        continue;
                    }
                    for (int c = 0; c < kCh; ++c) {
                        acc[c] += w.mask_w[static_cast<size_t>(j * kCh + c)] * x0[p * kCh + c];
                    }
                }
                float sum = w.mask_b;
                for (const float v : acc) {
                    sum += v;
                }
                mask[f] = kernels::sigmoid(sum);
            }
            mask[kErbIn] = mask[kErbIn - 2]; // reflect padding up to 481 bins
        }

        // ---- deep-filter decoder: five complex taps per bin below 4.8 kHz
        for (int k = 0; k < n; ++k) {
            grouped(w.df_in, s.enc.data() + k * kEmb, s.a.data() + k * kHidden);
        }
        kernels::relu(s.a.data(), n * kHidden);
        gru_x = s.a.data();
        for (size_t l = 0; l < 2; ++l) {
            for (int k = 0; k < n; ++k) {
                gru_states[static_cast<size_t>(k)] = states[k]->df_gru.data() + l * kHidden;
            }
            runGru(w.df_gru[l], n, gru_x, gru_states.data(), s.gru_in.data(), s);
            gru_x = s.gru_in.data();
        }
        for (int k = 0; k < n; ++k) {
            auto& st = *states[k];
            float* df = s.df.data() + k * kHidden;
            grouped(w.df_skip, s.enc.data() + k * kEmb, s.skip.data() + k * kHidden);
            for (int i = 0; i < kHidden; ++i) {
                df[i] = s.gru_in[static_cast<size_t>(k * kHidden + i)] + s.skip[static_cast<size_t>(k * kHidden + i)];
            }
            float* convp = s.convp.data() + k * kDfBins * kCoefs;
            for (int t = 0; t < kConvpTaps; ++t) {
                kernels::gemm(kDfBins, kCoefs, kCh, st.convp.data() + slot(st.frame, kConvpTaps - 1 - t, kConvpTaps) * kDfBins * kCh,
                              w.convp_w.data() + t * kCh * kCoefs, nullptr, convp, t > 0);
            }
        }
        linear(w.convp_pw, n * kDfBins, kCoefs, kCoefs, s.convp.data(), s.coefs.data(), true);
        for (int k = 0; k < n; ++k) {
            auto& st = *states[k];
            float* coefs = st.coefs.data() + slot(st.frame, 0, kCoefFrames) * kDfBins * kCoefs;
            grouped(w.df_out, s.df.data() + k * kHidden, coefs);
            kernels::tanh(coefs, kDfBins * kCoefs);
            add(coefs, s.coefs.data() + k * kDfBins * kCoefs, kDfBins * kCoefs);
        }

        // ---- the mask on the frame two hops back, then the deep filter centred two hops further back
        for (int k = 0; k < n; ++k) {
            auto& st = *states[k];
            const float* spec = st.spec.data() + slot(st.frame, kSpecFrames - 1, kSpecFrames) * kBins * 2;
            const float* mask = s.mask.data() + k * kBins;
            float* masked = st.masked.data() + slot(st.frame, 0, kMaskedFrames) * kBins * 2;
            for (int f = 0; f < kBins; ++f) {
                masked[2 * f] = spec[2 * f] * mask[f];
                masked[2 * f + 1] = spec[2 * f + 1] * mask[f];
            }
            const float* coefs = st.coefs.data() + slot(st.frame, kCoefFrames - 1, kCoefFrames) * kDfBins * kCoefs;
            const float* centre = st.masked.data() + slot(st.frame, 2, kMaskedFrames) * kBins * 2;
            float* o = out[k];
            for (int f = 0; f < kDfBins; ++f) {
                float re = 0.f, im = 0.f;
                for (int tap = 0; tap < kOrder; ++tap) {
                    const float* x = st.masked.data() + slot(st.frame, kOrder - 1 - tap, kMaskedFrames) * kBins * 2;
                    const float cr = coefs[f * kCoefs + 2 * tap], ci = coefs[f * kCoefs + 2 * tap + 1];
                    re += x[2 * f] * cr - x[2 * f + 1] * ci;
                    im += x[2 * f] * ci + x[2 * f + 1] * cr;
                }
                o[2 * f] = re * w.inv_wnorm;
                o[2 * f + 1] = im * w.inv_wnorm;
            }
            for (int f = kDfBins; f < kBins; ++f) {
                o[2 * f] = centre[2 * f] * w.inv_wnorm;
                o[2 * f + 1] = centre[2 * f + 1] * w.inv_wnorm;
            }
            ++st.frame;
        }
    }
}
