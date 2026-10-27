// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <clocale>
#include <model_data.hpp>

#include "dsp/engine.hpp"
#include "dsp/kernels.hpp"
#include "dsp/separator.hpp"

using namespace altvane::dialogue;

namespace {
    constexpr int kFrames = 64;

    /**
     * the STFT frames of a voiced, syllabic test signal over noise (the noise from a 32-bit LCG, so the reference
     * values below could be computed from exactly the same signal)
     */
    std::vector<dsp::Spectrum> testFrames() {
        constexpr int length = (kFrames + 1) * dsp::kHop;
        std::vector<float> x(length);
        double phase = 0.0;
        uint32_t lcg = 1;
        for (int i = 0; i < length; ++i) {
            const auto t = static_cast<double>(i) / dsp::kSampleRate;
            const auto syllable = std::fmod(t, .25) < .17 ? 1.0 : 0.0;
            phase += (140.0 + 30.0 * std::sin(2.0 * std::numbers::pi * 1.3 * t)) / dsp::kSampleRate;
            double voice = 0.0;
            for (int h = 1; h < 12; ++h) {
                voice += std::sin(2.0 * std::numbers::pi * phase * h) / h;
            }
            lcg = lcg * 1664525u + 1013904223u;
            x[static_cast<size_t>(i)] = static_cast<float>(.2 * syllable * voice + .03 * (lcg / 4294967296.0 * 2.0 - 1.0));
        }
        dsp::Fft fft;
        std::vector<dsp::Spectrum> frames(kFrames);
        std::array<float, dsp::kFftSize> windowed{};
        for (int f = 0; f < kFrames; ++f) {
            for (int n = 0; n < dsp::kFftSize; ++n) {
                const auto i = (f - 1) * dsp::kHop + n;
                windowed[static_cast<size_t>(n)] = i < 0 ? 0.f : x[static_cast<size_t>(i)] * dsp::vorbisWindow()[static_cast<size_t>(n)];
            }
            fft.forward(windowed, frames[static_cast<size_t>(f)]);
        }
        return frames;
    }

    /**
     * each stream's estimates of every frame; stream i hears the test signal scaled by 1 / (i + 1)
     */
    std::vector<std::vector<dsp::Spectrum>> separate(const dsp::Quality quality, const int streams) {
        const auto frames = testFrames();
        dsp::SeparatorStreams separator(dsp::SeparatorModel::get(quality), streams);
        dsp::SeparatorScratch scratch;
        std::vector<std::vector<dsp::Spectrum>> out(static_cast<size_t>(streams), std::vector<dsp::Spectrum>(kFrames));
        std::array<dsp::Spectrum, 2> in{};
        for (size_t f = 0; f < kFrames; ++f) {
            std::array<const dsp::Spectrum*, 2> ins{};
            std::array<dsp::Spectrum*, 2> outs{};
            for (size_t s = 0; s < static_cast<size_t>(streams); ++s) {
                for (size_t k = 0; k < dsp::kBins; ++k) {
                    in[s][k] = frames[f][k] / static_cast<float>(s + 1);
                }
                ins[s] = &in[s];
                outs[s] = &out[s][f];
            }
            separator.process(std::span(ins.data(), static_cast<size_t>(streams)),
                              std::span(outs.data(), static_cast<size_t>(streams)), scratch);
        }
        return out;
    }

    double energy(const dsp::Spectrum& x) {
        double e = 0.0;
        for (const auto& v : x) {
            e += std::norm(std::complex<double>(v));
        }
        return e;
    }

    float largestDifference(const std::vector<dsp::Spectrum>& a, const std::vector<dsp::Spectrum>& b) {
        float worst = 0.f;
        for (size_t f = 0; f < a.size(); ++f) {
            for (size_t k = 0; k < dsp::kBins; ++k) {
                worst = std::max(worst, std::abs(a[f][k] - b[f][k]));
            }
        }
        return worst;
    }
}

TEST_CASE("the network computes what the ONNX graph computes", "[model]") {
    // from ONNX Runtime 1.31 on the same signal (energies of frames 20, 40 and 63; frame 63 at bins 3, 50, 95, 96,
    // 250 and 480). Given identical frames the two agree to about 130 dB; here the frames come from our FFT rather
    // than numpy's, which differs in the last bits, and the model carries that through to about 1e-4
    struct Reference {
        dsp::Quality quality;
        std::array<double, 3> energies;
        std::array<dsp::Complex, 6> bins;
    };
    const auto ref = GENERATE(
        Reference{dsp::Quality::kEco, {6684.5062, 7744.31371, 6734.50577},
                  {{{26.5619392f, 26.2541733f}, {-0.00134730153f, -0.00161832431f}, {-0.000940206053f, 0.000215758482f},
                    {-0.000834017934f, 0.00212324876f}, {-0.000774389773f, -0.00016828021f}, {-0.00160864298f, 0.f}}}},
        Reference{dsp::Quality::kHigh, {6556.48586, 7575.81566, 6417.45376},
                  {{{25.2264557f, 25.1068993f}, {-0.00142252876f, -0.00420723064f}, {-0.00111350638f, 0.000182225151f},
                    {-0.000875132624f, 0.00222791871f}, {-0.000632442359f, -0.000137434079f}, {-0.000975272385f, 0.f}}}});
    CAPTURE(ref.quality == dsp::Quality::kHigh);
    const auto out = separate(ref.quality, 1)[0];
    const std::array<size_t, 3> frames{20, 40, 63};
    for (size_t i = 0; i < 3; ++i) {
        CAPTURE(frames[i]);
        CHECK(std::abs(energy(out[frames[i]]) / ref.energies[i] - 1.0) < 2e-3);
    }
    const std::array<size_t, 6> bins{3, 50, 95, 96, 250, 480};
    for (size_t i = 0; i < 6; ++i) {
        CAPTURE(bins[i]);
        CHECK(std::abs(out[63][bins[i]] - ref.bins[i]) < 1e-2f);
    }
}

TEST_CASE("stereo streams run together give what each gives alone", "[model]") {
    const auto quality = GENERATE(dsp::Quality::kEco, dsp::Quality::kHigh);
    const auto together = separate(quality, 2);
    const auto alone = separate(quality, 1);
    CHECK(largestDifference(together[0], alone[0]) < 1e-4f);
    // the second stream alone: the same frames at half the level, as stream 0
    const auto frames = testFrames();
    dsp::SeparatorStreams separator(dsp::SeparatorModel::get(quality), 1);
    dsp::SeparatorScratch scratch;
    std::vector<dsp::Spectrum> second(kFrames);
    for (size_t f = 0; f < kFrames; ++f) {
        dsp::Spectrum in{};
        for (size_t k = 0; k < dsp::kBins; ++k) {
            in[k] = frames[f][k] * .5f;
        }
        const dsp::Spectrum* ins[] = {&in};
        dsp::Spectrum* outs[] = {&second[f]};
        separator.process(ins, outs, scratch);
    }
    CHECK(largestDifference(together[1], second) < 1e-4f);
}

TEST_CASE("the BLAS and portable matrix products agree", "[model]") {
    // only differs where there is a BLAS (Accelerate on macOS)
    const auto quality = GENERATE(dsp::Quality::kEco, dsp::Quality::kHigh);
    const auto with_blas = separate(quality, 2);
    dsp::kernels::setBlasEnabled(false);
    const auto portable = separate(quality, 2);
    dsp::kernels::setBlasEnabled(true);
    CHECK(largestDifference(with_blas[0], portable[0]) < 1e-3f);
    CHECK(largestDifference(with_blas[1], portable[1]) < 1e-3f);
}

TEST_CASE("the activation approximations are accurate", "[model]") {
    float tanh_error = 0.f, sigmoid_error = 0.f;
    for (int i = -200000; i <= 200000; ++i) {
        const auto x = static_cast<float>(i) * 1e-4f;
        tanh_error = std::max(tanh_error, static_cast<float>(std::abs(dsp::kernels::tanh(x) - std::tanh(static_cast<double>(x)))));
        sigmoid_error = std::max(sigmoid_error, static_cast<float>(std::abs(dsp::kernels::sigmoid(x) - 1.0 / (1.0 + std::exp(-static_cast<double>(x))))));
    }
    CHECK(tanh_error < 5e-7f);
    CHECK(sigmoid_error < 5e-7f);
}

TEST_CASE("the latency is the model's delay, then a hop or a block for the worker", "[engine]") {
    const auto block = GENERATE(32, 64, 100, 128, 256, 480, 512, 1024, 4096);
    CAPTURE(block);
    dsp::Engine engine;
    engine.prepare(2, block);
    const auto budget = engine.getLatency() - dsp::kAlgorithmicLatency;
    // the worker gets at least a hop of wall-clock time, and whole blocks of it
    CHECK(budget >= std::max(block, dsp::kHop));
    CHECK(budget % block == 0);
    CHECK(budget < std::max(block, dsp::kHop) + block);
    engine.release();
}

TEST_CASE("the models load under a locale with a decimal comma", "[model]") {
    // the normalisation values in the model metadata are written with decimal points
    const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
    const char* comma = nullptr;
    for (const auto* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "German_Germany.1252"}) {
        if (std::setlocale(LC_NUMERIC, name) != nullptr) {
            comma = name;
            break;
        }
    }
    if (comma == nullptr) {
        SKIP("no locale with a decimal comma is installed");
    }
    CAPTURE(comma);
    CHECK_NOTHROW(dsp::DpdfNet(models::dpdfnet2()));
    std::setlocale(LC_NUMERIC, previous.c_str());
}
