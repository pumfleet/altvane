// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "analyzer.hpp"

#include <pffft.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace altvane::deesser {
    namespace {
        // where the finder looks for sibilance
        constexpr float kFinderLowHz = 2000.f;
        constexpr float kFinderHighHz = 16000.f;
        // seconds of esses heard before the finder shows, and before it is sure
        constexpr double kFinderShowSeconds = .15;
        constexpr double kFinderSureSeconds = .5;
        constexpr float kReleaseDbPerFrame = 2.5f;
    }

    Analyzer::Analyzer() {
        setup_ = pffft_new_setup(kFFTSize, PFFFT_REAL);
        in_ = static_cast<float*>(pffft_aligned_malloc(kFFTSize * sizeof(float)));
        out_ = static_cast<float*>(pffft_aligned_malloc(kFFTSize * sizeof(float)));
        work_ = static_cast<float*>(pffft_aligned_malloc(kFFTSize * sizeof(float)));
        window_.resize(kFFTSize);
        double sum = 0.0;
        for (int i = 0; i < kFFTSize; ++i) {
            window_[static_cast<size_t>(i)] = static_cast<float>(.5 - .5 * std::cos(2.0 * std::numbers::pi * i / kFFTSize));
            sum += window_[static_cast<size_t>(i)];
        }
        // a full-scale sine reads 0 dB
        for (auto& w : window_) {
            w = static_cast<float>(w * 2.0 / sum);
        }
        for (auto& r : ring_) {
            r.assign(kFFTSize, 0.f);
        }
        flags_.assign(kFFTSize, 0.f);
        for (auto& p : power_) {
            p.assign(kFFTSize / 2 + 1, 0.f);
        }
        pre_.fill(kSilenceDb);
        post_.fill(kSilenceDb);
        resetSibilance();
    }

    Analyzer::~Analyzer() {
        pffft_aligned_free(in_);
        pffft_aligned_free(out_);
        pffft_aligned_free(work_);
        pffft_destroy_setup(setup_);
    }

    float Analyzer::pointHz(const int point) {
        return kLowHz * std::pow(kHighHz / kLowHz, static_cast<float>(point) / static_cast<float>(kPoints - 1));
    }

    void Analyzer::prepare(const double sample_rate) {
        sample_rate_ = sample_rate;
        const auto bin_hz = sample_rate / kFFTSize;
        const auto half_step = std::pow(static_cast<double>(kHighHz / kLowHz), .5 / (kPoints - 1));
        for (int p = 0; p < kPoints; ++p) {
            const auto hz = static_cast<double>(pointHz(p));
            const auto lo = hz / half_step / bin_hz, hi = hz * half_step / bin_hz;
            auto& span = spans_[static_cast<size_t>(p)];
            if (std::floor(hi) - std::ceil(lo) >= 1.0) {
                span = {static_cast<int>(std::ceil(lo)), std::min(static_cast<int>(std::floor(hi)), kFFTSize / 2), -1.f};
            } else {
                const auto exact = hz / bin_hz;
                const auto first = std::min(static_cast<int>(exact), kFFTSize / 2 - 1);
                span = {first, first + 1, static_cast<float>(exact - first)};
            }
        }
        for (auto& r : ring_) {
            std::fill(r.begin(), r.end(), 0.f);
        }
        std::fill(flags_.begin(), flags_.end(), 0.f);
        write_ = 0;
        fresh_ = 0;
        pre_.fill(kSilenceDb);
        post_.fill(kSilenceDb);
        resetSibilance();
    }

    void Analyzer::resetSibilance() {
        sibilance_.fill(kSilenceDb);
        sibilance_power_.fill(0.0);
        sibilance_frames_ = 0;
        finder_ = {};
    }

    bool Analyzer::process(dsp::Engine& engine) {
        if (engine.getSampleRate() != sample_rate_) {
            prepare(engine.getSampleRate());
        }
        bool computed = false;
        dsp::AnalysisSample s;
        while (engine.pullAnalysis(s)) {
            ring_[0][write_] = s.side;
            ring_[1][write_] = s.out;
            flags_[write_] = s.sibilant;
            write_ = (write_ + 1) % kFFTSize;
            if (++fresh_ >= kHop) {
                fresh_ = 0;
                computeFrame();
                computed = true;
            }
        }
        return computed;
    }

    void Analyzer::spectrum(const std::vector<float>& ring, std::vector<float>& power) {
        for (size_t i = 0; i < kFFTSize; ++i) {
            in_[i] = ring[(write_ + i) % kFFTSize] * window_[i];
        }
        pffft_transform_ordered(setup_, in_, out_, work_, PFFFT_FORWARD);
        power[0] = out_[0] * out_[0];
        power[kFFTSize / 2] = out_[1] * out_[1];
        for (size_t k = 1; k < kFFTSize / 2; ++k) {
            power[k] = out_[2 * k] * out_[2 * k] + out_[2 * k + 1] * out_[2 * k + 1];
        }
    }

    float Analyzer::pointPower(const std::vector<float>& power, const int point) const {
        const auto& span = spans_[static_cast<size_t>(point)];
        if (span.frac < 0.f) {
            // the loudest bin it covers, so a tone reads its level wherever it falls
            float most = 0.f;
            for (int b = span.first; b <= span.last; ++b) {
                most = std::max(most, power[static_cast<size_t>(b)]);
            }
            return most;
        }
        return power[static_cast<size_t>(span.first)] * (1.f - span.frac) + power[static_cast<size_t>(span.last)] * span.frac;
    }

    void Analyzer::computeFrame() {
        spectrum(ring_[0], power_[0]);
        spectrum(ring_[1], power_[1]);
        float flagged = 0.f;
        for (const auto f : flags_) {
            flagged += f;
        }
        const auto sibilant = flagged > .3f * kFFTSize;
        if (sibilant) {
            ++sibilance_frames_;
        }
        const auto nyquist_point = [this](const int p) { return pointHz(p) < .48 * sample_rate_; };
        // a running average over the esses heard, settling on about the last second of them
        const auto smooth = sibilant ? std::max(static_cast<double>(kHop) / sample_rate_, 1.0 / sibilance_frames_) : 0.0;
        for (int p = 0; p < kPoints; ++p) {
            const auto i = static_cast<size_t>(p);
            if (!nyquist_point(p)) {
                pre_[i] = post_[i] = kSilenceDb;
                continue;
            }
            const auto pre_db = 10.f * std::log10(pointPower(power_[0], p) + 1e-15f);
            const auto post_db = 10.f * std::log10(pointPower(power_[1], p) + 1e-15f);
            pre_[i] = pre_db > pre_[i] ? pre_db : std::max(pre_db, pre_[i] - kReleaseDbPerFrame);
            post_[i] = post_db > post_[i] ? post_db : std::max(post_db, post_[i] - kReleaseDbPerFrame);
            if (sibilant) {
                sibilance_power_[i] += (pointPower(power_[0], p) - sibilance_power_[i]) * smooth;
                sibilance_[i] = 10.f * static_cast<float>(std::log10(sibilance_power_[i] + 1e-15));
            }
        }
        if (sibilant) {
            updateFinder();
        }
    }

    void Analyzer::updateFinder() {
        const auto heard = sibilance_frames_ * static_cast<double>(kHop) / sample_rate_;
        if (heard < kFinderShowSeconds) {
            return;
        }
        // the peak of the esses' spectrum, smoothed over about a third of an octave
        constexpr int kHalfWidth = 4;
        auto best = -1e30f;
        int best_point = -1;
        for (int p = kHalfWidth; p < kPoints - kHalfWidth; ++p) {
            const auto hz = pointHz(p);
            if (hz < kFinderLowHz || hz > std::min(kFinderHighHz, static_cast<float>(.45 * sample_rate_))) {
                continue;
            }
            double sum = 0.0;
            for (int q = p - kHalfWidth; q <= p + kHalfWidth; ++q) {
                sum += sibilance_power_[static_cast<size_t>(q)];
            }
            if (const auto level = static_cast<float>(sum); level > best) {
                best = level;
                best_point = p;
            }
        }
        if (best_point < 0 || best <= 0.f) {
            return;
        }
        finder_.hz = pointHz(best_point);
        finder_.confidence = static_cast<float>(std::min(1.0, heard / kFinderSureSeconds));
    }
}
