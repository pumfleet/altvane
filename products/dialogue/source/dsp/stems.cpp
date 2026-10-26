// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "stems.hpp"

#include <algorithm>
#include <cmath>

namespace altvane::dialogue::dsp {
    namespace {
        // decay prior for the late reverb: the split is insensitive to it (0.4 .. 1.2 s rooms score alike),
        // so a fixed value beats a noisy blind estimate
        constexpr float kAssumedT60 = .8f;
        // how much of the voice's energy returns as late reverb (direct-to-reverberant ratio prior)
        constexpr float kReverbLevel = .3f;
        constexpr float kSmoothing = .5f;
        constexpr float kEpsilon = 1e-12f;
    }

    StemSplitter::StemSplitter() {
        const auto alpha = 6.9f / kAssumedT60;
        constexpr auto early = static_cast<float>(kEarlyFrames * kHop) / static_cast<float>(kSampleRate);
        late_gain_ = kReverbLevel * std::exp(-2.f * alpha * early);
        reset();
    }

    void StemSplitter::reset() {
        smooth_speech_.fill(0.f);
        smooth_rest_.fill(0.f);
        floor_.fill(1e-9f);
        for (auto& h : voice_history_) {
            h.fill(0.f);
        }
        history_pos_ = 0;
    }

    void StemSplitter::split(const std::span<const Complex, kBins> x, const std::span<const Complex, kBins> speech,
                             StemShares& out) {
        history_pos_ = (history_pos_ + 1) % static_cast<int>(voice_history_.size());
        auto& voice_now = voice_history_[static_cast<size_t>(history_pos_)];
        // the oldest entry: the voice kEarlyFrames ago
        const auto& voice_then = voice_history_[static_cast<size_t>((history_pos_ + 1) % static_cast<int>(voice_history_.size()))];

        for (size_t k = 0; k < kBins; ++k) {
            const auto xk = x[k];
            const auto sk = speech[k];
            const auto rest = xk - sk;
            auto& ss = smooth_speech_[k];
            auto& sr = smooth_rest_[k];
            ss = kSmoothing * ss + (1.f - kSmoothing) * std::norm(sk);
            sr = kSmoothing * sr + (1.f - kSmoothing) * std::norm(rest);
            // the ambience floor: falls quickly, rises slowly
            auto& fl = floor_[k];
            fl = sr < fl ? .7f * fl + .3f * sr : fl * 1.02f;
            // the voice, direct and reverberant, and the late reverb it produced kEarlyFrames ago
            voice_now[k] = std::max(ss, ss + sr - fl);
            const auto late = late_gain_ * voice_then[k];
            const auto g_speech = std::clamp(.5f * late / (ss + kEpsilon), 0.f, 1.f);
            const auto g_rest = std::clamp(.5f * late / (sr + kEpsilon), 0.f, 1.f);

            // the speech estimate as a complex share of the input
            const auto x_energy = std::norm(xk);
            const auto s_share = x_energy > 1e-20f ? sk * std::conj(xk) / x_energy : Complex{1.f, 0.f};
            const auto r_share = Complex{1.f, 0.f} - s_share;
            out.share[kVoice][k] = s_share * (1.f - g_speech);
            out.share[kReverb][k] = s_share * g_speech + r_share * g_rest;
            out.share[kAmbience][k] = r_share * (1.f - g_rest);
        }
    }

    void computeSensitivity(const std::array<float, kSensitivityNodes>& nodes, BinWeights& weights) {
        static const auto node_x = [] {
            std::array<float, kSensitivityNodes> x{};
            for (size_t i = 0; i < x.size(); ++i) {
                x[i] = std::log2(kSensitivityFrequencies[i]);
            }
            return x;
        }();
        constexpr float kRange = 1.3862944f; // ln 4: +-100% is x4 / x0.25
        for (int k = 0; k < kBins; ++k) {
            const auto f = std::max(binFrequency(k), 1.f);
            const auto x = std::log2(f);
            float v;
            if (x <= node_x.front()) {
                v = nodes.front();
            } else if (x >= node_x.back()) {
                v = nodes.back();
            } else {
                size_t i = 0;
                while (x > node_x[i + 1]) {
                    ++i;
                }
                const auto t = (x - node_x[i]) / (node_x[i + 1] - node_x[i]);
                const auto smooth = t * t * (3.f - 2.f * t);
                v = nodes[i] + (nodes[i + 1] - nodes[i]) * smooth;
            }
            weights[static_cast<size_t>(k)] = std::exp(kRange * std::clamp(v, -1.f, 1.f));
        }
    }

    void computeTransfer(const StemShares& shares, const std::array<BinWeights, kNumStems>& weights,
                         const std::array<float, kNumStems>& gains, const Listen listen,
                         Spectrum& transfer, StemShares& weighted) {
        for (size_t k = 0; k < kBins; ++k) {
            // reweight the shares and renormalise, so they still sum to one
            Complex sum{};
            for (size_t s = 0; s < kNumStems; ++s) {
                sum += weights[s][k] * shares.share[s][k];
            }
            if (std::norm(sum) > 1e-2f) {
                const auto inv = Complex{1.f, 0.f} / sum;
                for (size_t s = 0; s < kNumStems; ++s) {
                    weighted.share[s][k] = weights[s][k] * shares.share[s][k] * inv;
                }
            } else {
                for (size_t s = 0; s < kNumStems; ++s) {
                    weighted.share[s][k] = shares.share[s][k];
                }
            }

            Complex mix{};
            for (size_t s = 0; s < kNumStems; ++s) {
                mix += gains[s] * weighted.share[s][k];
            }
            switch (listen) {
                case Listen::kMix: transfer[k] = mix;
                    break;
                case Listen::kVoice: transfer[k] = weighted.share[kVoice][k];
                    break;
                case Listen::kAmbience: transfer[k] = weighted.share[kAmbience][k];
                    break;
                case Listen::kReverb: transfer[k] = weighted.share[kReverb][k];
                    break;
                case Listen::kRemoved: transfer[k] = Complex{1.f, 0.f} - mix;
                    break;
            }
        }
    }
}
