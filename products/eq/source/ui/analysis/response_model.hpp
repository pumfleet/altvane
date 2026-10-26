// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "altvane_ui/util/freq_scale.hpp"
#include "altvane_ui/util/spring.hpp"
#include "../model/engine_bridge.hpp"
#include "../../dsp/filter/iir_filter/coeff/ivantsov_coeff.hpp"

namespace altvane::eq {
    /**
     * magnitude responses of every band and their per-stereo-mode sums, in dB, exactly as the engine applies them:
     * - minimum phase / SVF run digital biquads: their coefficients from FilterDesign::updateCoeffs<IvantsovCoeff>
     *   are evaluated on the unit circle
     * - parallel runs low-order peaks and shelves as parallel branches, y = x + sum((g - 1) * branch(x)),
     *   followed by the remaining bands in series, so those branches are summed as complex responses
     * - matched / mixed / zero phase are FIR-corrected towards the analog prototype, so zldsp::filter::Ideal is used
     * evaluated on the message thread; one band costs well under 0.1 ms, so curves stay frame-locked with the nodes
     * displayed curves glide towards new responses (undo, preset load, typed values) unless an edit is in progress
     */
    class ResponseModel {
    public:
        static constexpr size_t kNumStereo = 5;

        ResponseModel();

        void setNumPoints(size_t num_points);

        size_t getNumPoints() const { return freqs_.size(); }

        double getFreq(const size_t i) const { return freqs_[i]; }

        /**
         * recompute changed bands and advance the morph animation
         * @return whether the displayed curves changed
         */
        bool update(EngineBridge& bridge, double dt, bool snap);

        const std::vector<float>& getBandDB(const size_t band) const { return display_[band]; }
        const std::vector<float>& getBandStaticDB(const size_t band) const { return base_[band]; }
        const std::vector<float>& getBandTargetDB(const size_t band) const { return target_[band]; }
        const std::vector<float>& getSumDB(const size_t lr) const { return sum_display_[lr]; }

        bool isStereoModeUsed(const size_t lr) const { return lr_used_[lr]; }

        /**
         * live gain of a dynamic band relative to its static gain, in dB
         */
        float getDynamicDelta(const size_t band) const { return dynamic_delta_[band]; }

    private:
        enum class Mode { kDigital, kParallel, kIdeal };

        using IdealFilter = zldsp::filter::Ideal<float, zlp::Controller::kFilterSize>;
        using Coeffs = std::array<std::array<double, 5>, zlp::Controller::kFilterSize>;

        double sample_rate_{0.0};
        float gain_scale_{100.f};
        Mode mode_{Mode::kDigital};
        std::vector<double> freqs_;
        std::vector<float> ws_;
        std::vector<double> cos1_, cos2_, sin1_, sin2_;

        std::array<IdealFilter, kBandNum> ideal_{};
        Coeffs coeffs_{};
        // per band responses in dB: static gain, dynamic target gain, live gain, and the one currently in effect
        std::array<std::vector<float>, kBandNum> base_, target_, live_, resolved_, display_;
        // parallel branches: complex branch response and the multipliers (g - 1) for each gain state
        std::array<bool, kBandNum> is_branch_{};
        std::array<std::vector<double>, kBandNum> branch_re_, branch_im_;
        std::array<double, kBandNum> branch_m_{}, resolved_m_{};
        std::array<std::vector<float>, kNumStereo> sum_target_, sum_display_;
        std::vector<double> scratch_re_, scratch_im_;
        std::array<bool, kNumStereo> lr_used_{};
        std::array<float, kBandNum> dynamic_delta_{};
        std::array<bool, kBandNum> was_active_{};
        bool needs_full_update_{true};
        bool animating_{false};

        static float scaledGain(float gain, float scale) {
            return std::clamp(gain * scale / 100.f, -30.f, 30.f);
        }

        zldsp::filter::FilterParameters getParas(const BandSnapshot& b, float gain) const;

        bool isBranch(const BandSnapshot& b) const;

        void computeMagnitude(const BandSnapshot& b, size_t band, float gain, std::vector<float>& out);

        void computeBranch(const BandSnapshot& b, size_t band);

        void branchToDB(size_t band, double m, std::vector<float>& out) const;

        void computeSums(const EngineBridge& bridge);
    };
}
