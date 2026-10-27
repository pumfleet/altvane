// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "voicing.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace altvane::leveler::dsp {
    namespace {
        constexpr double kTargetRate = 8000.0;
        constexpr double kWindowSeconds = .02;
        constexpr double kMinPitch = 70.0, kMaxPitch = 800.0;
        constexpr double kHighBand = 4000.0;
    }

    void VoicingDetector::prepare(const double sample_rate, const int) {
        decimation_ = std::max(1, static_cast<int>(std::lround(sample_rate / kTargetRate)));
        const auto rate = sample_rate / decimation_;
        // fourth-order Butterworth anti-alias filter well below the decimated Nyquist
        const auto cutoff = std::min(3000.0, rate * .4);
        anti_alias_a_.setCoefficients(lowPass(sample_rate, cutoff, .5411961));
        anti_alias_b_.setCoefficients(lowPass(sample_rate, cutoff, 1.3065630));
        has_high_band_ = sample_rate * .5 > kHighBand * 1.25;
        high_.setCoefficients(highPass(sample_rate, has_high_band_ ? kHighBand : sample_rate * .2, .7071068));

        window_ = static_cast<size_t>(std::lround(kWindowSeconds * rate));
        min_lag_ = static_cast<size_t>(std::floor(rate / kMaxPitch));
        max_lag_ = static_cast<size_t>(std::ceil(rate / kMinPitch));
        frame_.assign(window_ + max_lag_, 0.f);
        diff_.assign(max_lag_ + 1, 0.f);
        reset();
    }

    void VoicingDetector::reset() {
        anti_alias_a_.reset();
        anti_alias_b_.reset();
        high_.reset();
        ring_.fill(0.f);
        write_ = 0;
        phase_ = 0;
        high_energy_.fill(0.0);
        total_energy_.fill(0.0);
        energy_slot_ = 0;
        hop_high_ = hop_total_ = 0.0;
    }

    void VoicingDetector::push(const float x) {
        const auto lp = anti_alias_b_.process(anti_alias_a_.process(x));
        if (++phase_ >= decimation_) {
            phase_ = 0;
            ring_[write_++ & (kRingSize - 1)] = static_cast<float>(lp);
        }
        const auto h = has_high_band_ ? high_.process(x) : 0.0;
        hop_high_ += h * h;
        hop_total_ += static_cast<double>(x) * x;
    }

    VoicingDetector::Result VoicingDetector::analyse() {
        Result result;
        high_energy_[energy_slot_] = hop_high_;
        total_energy_[energy_slot_] = hop_total_;
        energy_slot_ = (energy_slot_ + 1) % kEnergyHops;
        hop_high_ = hop_total_ = 0.0;
        const auto total = std::accumulate(total_energy_.begin(), total_energy_.end(), 0.0);
        if (total > 1e-12) {
            result.high_ratio = static_cast<float>(
                std::clamp(std::accumulate(high_energy_.begin(), high_energy_.end(), 0.0) / total, 0.0, 1.0));
        }

        // the latest window + max lag decimated samples, oldest first
        const auto n = frame_.size();
        for (size_t i = 0; i < n; ++i) {
            frame_[i] = ring_[(write_ - n + i) & (kRingSize - 1)];
        }
        double power = 0.0;
        for (size_t j = 0; j < window_; ++j) {
            power += static_cast<double>(frame_[j]) * frame_[j];
        }
        if (power < 1e-9 * static_cast<double>(window_)) {
            return result;
        }
        // YIN: d(tau) = sum (x[j] - x[j + tau])^2, then normalised by its running mean
        double running = 0.0;
        float best = 1.f;
        for (size_t tau = 1; tau <= max_lag_; ++tau) {
            float d = 0.f;
            const auto* a = frame_.data();
            const auto* b = frame_.data() + tau;
            for (size_t j = 0; j < window_; ++j) {
                const auto e = a[j] - b[j];
                d += e * e;
            }
            running += d;
            const auto normalised = running > 0.0 ? static_cast<float>(d * static_cast<double>(tau) / running) : 1.f;
            if (tau >= min_lag_) {
                best = std::min(best, normalised);
            }
        }
        result.periodicity = std::clamp(1.f - best, 0.f, 1.f);
        return result;
    }
}
