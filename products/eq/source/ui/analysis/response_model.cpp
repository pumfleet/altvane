// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "response_model.hpp"

#include <numbers>

namespace altvane::eq {
    using zldsp::filter::FilterType;

    ResponseModel::ResponseModel() {
        setNumPoints(512);
    }

    void ResponseModel::setNumPoints(const size_t num_points) {
        if (num_points == freqs_.size()) {
            return;
        }
        freqs_.resize(num_points);
        ws_.resize(num_points);
        for (auto* v : {&cos1_, &cos2_, &sin1_, &sin2_, &scratch_re_, &scratch_im_}) {
            v->resize(num_points);
        }
        for (size_t i = 0; i < num_points; ++i) {
            freqs_[i] = FreqScale::pointToFreq(i, num_points);
        }
        for (size_t band = 0; band < kBandNum; ++band) {
            for (auto* v : {&base_[band], &target_[band], &live_[band], &resolved_[band], &display_[band]}) {
                v->assign(num_points, 0.f);
            }
            branch_re_[band].assign(num_points, 0.0);
            branch_im_[band].assign(num_points, 0.0);
        }
        for (size_t lr = 0; lr < kNumStereo; ++lr) {
            sum_target_[lr].assign(num_points, 0.f);
            sum_display_[lr].assign(num_points, 0.f);
        }
        sample_rate_ = 0.0;
        needs_full_update_ = true;
    }

    zldsp::filter::FilterParameters ResponseModel::getParas(const BandSnapshot& b, const float gain) const {
        return {
            b.type, b.getOrder(),
            std::min(static_cast<double>(b.freq), 0.49964 * sample_rate_),
            static_cast<double>(gain), static_cast<double>(b.q)
        };
    }

    bool ResponseModel::isBranch(const BandSnapshot& b) const {
        // mirrors zldsp::filter::Parallel::updateCoeffs
        if (mode_ != Mode::kParallel) {
            return false;
        }
        const auto order = b.getOrder();
        return (b.type == FilterType::kPeak && order <= 4)
            || ((b.type == FilterType::kLowShelf || b.type == FilterType::kHighShelf) && order <= 2);
    }

    void ResponseModel::computeMagnitude(const BandSnapshot& b, const size_t band, const float gain,
                                         std::vector<float>& out) {
        const auto para = getParas(b, gain);
        if (mode_ == Mode::kIdeal) {
            ideal_[band].forceUpdate(para);
            ideal_[band].updateMagnitudeSquare(ws_, out);
            zldsp::vector::sqr_mag_to_db(out.data(), out.size());
            return;
        }
        const auto num = zldsp::filter::FilterDesign::updateCoeffs<zldsp::filter::IvantsovCoeff>(
            para.filter_type, para.order, para.freq, sample_rate_, para.gain, para.q, coeffs_);
        for (size_t i = 0; i < out.size(); ++i) {
            const auto c1 = cos1_[i], c2 = cos2_[i];
            double mag_sqr = 1.0;
            for (size_t k = 0; k < num; ++k) {
                // coefficient layout: a1, a2, b0, b1, b2 (a0 = 1)
                const auto& c = coeffs_[k];
                const auto num_sqr = c[2] * c[2] + c[3] * c[3] + c[4] * c[4]
                    + 2.0 * (c[2] * c[3] + c[3] * c[4]) * c1 + 2.0 * c[2] * c[4] * c2;
                const auto den_sqr = 1.0 + c[0] * c[0] + c[1] * c[1]
                    + 2.0 * (c[0] + c[0] * c[1]) * c1 + 2.0 * c[1] * c2;
                mag_sqr *= num_sqr / std::max(den_sqr, 1e-300);
            }
            out[i] = static_cast<float>(10.0 * std::log10(std::max(mag_sqr, 1e-30)));
        }
    }

    void ResponseModel::computeBranch(const BandSnapshot& b, const size_t band) {
        auto para = getParas(b, 0.f);
        para.filter_type = b.type == FilterType::kPeak
            ? FilterType::kBandPass
            : (b.type == FilterType::kLowShelf ? FilterType::kLowPass : FilterType::kHighPass);
        const auto num = zldsp::filter::FilterDesign::updateCoeffs<zldsp::filter::IvantsovCoeff>(
            para.filter_type, para.order, para.freq, sample_rate_, para.gain, para.q, coeffs_);
        auto& re = branch_re_[band];
        auto& im = branch_im_[band];
        for (size_t i = 0; i < re.size(); ++i) {
            // z^-1 = cos w - j sin w
            const auto c1 = cos1_[i], c2 = cos2_[i], s1 = sin1_[i], s2 = sin2_[i];
            double h_re = 1.0, h_im = 0.0;
            for (size_t k = 0; k < num; ++k) {
                const auto& c = coeffs_[k];
                const auto n_re = c[2] + c[3] * c1 + c[4] * c2;
                const auto n_im = -(c[3] * s1 + c[4] * s2);
                const auto d_re = 1.0 + c[0] * c1 + c[1] * c2;
                const auto d_im = -(c[0] * s1 + c[1] * s2);
                const auto d_mag = std::max(d_re * d_re + d_im * d_im, 1e-300);
                const auto q_re = (n_re * d_re + n_im * d_im) / d_mag;
                const auto q_im = (n_im * d_re - n_re * d_im) / d_mag;
                const auto t_re = h_re * q_re - h_im * q_im;
                h_im = h_re * q_im + h_im * q_re;
                h_re = t_re;
            }
            re[i] = h_re;
            im[i] = h_im;
        }
    }

    void ResponseModel::branchToDB(const size_t band, const double m, std::vector<float>& out) const {
        const auto& re = branch_re_[band];
        const auto& im = branch_im_[band];
        for (size_t i = 0; i < out.size(); ++i) {
            const auto y_re = 1.0 + m * re[i];
            const auto y_im = m * im[i];
            out[i] = static_cast<float>(10.0 * std::log10(std::max(y_re * y_re + y_im * y_im, 1e-30)));
        }
    }

    void ResponseModel::computeSums(const EngineBridge& bridge) {
        for (size_t lr = 0; lr < kNumStereo; ++lr) {
            lr_used_[lr] = false;
            auto& s = sum_target_[lr];
            std::fill(s.begin(), s.end(), 0.f);
            std::fill(scratch_re_.begin(), scratch_re_.end(), 1.0);
            std::fill(scratch_im_.begin(), scratch_im_.end(), 0.0);
            bool any_branch = false;
            for (size_t band = 0; band < kBandNum; ++band) {
                const auto& b = bridge.band(band);
                if (!b.isActive() || static_cast<size_t>(b.lr) != lr) {
                    continue;
                }
                lr_used_[lr] = true;
                if (!b.isOn()) {
                    continue;
                }
                if (is_branch_[band]) {
                    any_branch = true;
                    const auto m = resolved_m_[band];
                    for (size_t i = 0; i < s.size(); ++i) {
                        scratch_re_[i] += m * branch_re_[band][i];
                        scratch_im_[i] += m * branch_im_[band][i];
                    }
                } else {
                    const auto& r = resolved_[band];
                    for (size_t i = 0; i < s.size(); ++i) {
                        s[i] += r[i];
                    }
                }
            }
            if (any_branch) {
                for (size_t i = 0; i < s.size(); ++i) {
                    s[i] += static_cast<float>(10.0 * std::log10(std::max(
                        scratch_re_[i] * scratch_re_[i] + scratch_im_[i] * scratch_im_[i], 1e-30)));
                }
            }
        }
    }

    bool ResponseModel::update(EngineBridge& bridge, const double dt, const bool snap) {
        auto changed = bridge.getChangedBands();

        if (const auto sr = bridge.getSampleRate(); std::abs(sr - sample_rate_) > 1.0) {
            sample_rate_ = sr;
            for (auto& f : ideal_) {
                f.prepare(sr);
            }
            const auto scale = 2.0 * std::numbers::pi / sr;
            for (size_t i = 0; i < freqs_.size(); ++i) {
                ws_[i] = static_cast<float>(freqs_[i] * scale);
                // a digital filter has no response above Nyquist; hold the value at Nyquist
                const auto w = std::min(freqs_[i] * scale, std::numbers::pi);
                cos1_[i] = std::cos(w);
                cos2_[i] = std::cos(2.0 * w);
                sin1_[i] = std::sin(w);
                sin2_[i] = std::sin(2.0 * w);
            }
            needs_full_update_ = true;
        }
        const auto structure = bridge.global().structure;
        const auto mode = structure == zlp::kMinimum || structure == zlp::kSVF
            ? Mode::kDigital
            : (structure == zlp::kParallel ? Mode::kParallel : Mode::kIdeal);
        if (mode != mode_) {
            mode_ = mode;
            needs_full_update_ = true;
        }
        if (std::abs(bridge.global().gain_scale - gain_scale_) > 1e-3f) {
            gain_scale_ = bridge.global().gain_scale;
            needs_full_update_ = true;
        }
        if (needs_full_update_) {
            changed.set();
            needs_full_update_ = false;
        }

        bool targets_changed = false;
        auto& controller = bridge.getController();
        for (size_t band = 0; band < kBandNum; ++band) {
            const auto& b = bridge.band(band);
            if (!b.isActive()) {
                if (was_active_[band]) {
                    was_active_[band] = false;
                    targets_changed = true;
                }
                continue;
            }
            const auto static_gain = scaledGain(b.gain, gain_scale_);
            if (changed[band]) {
                is_branch_[band] = isBranch(b);
                if (is_branch_[band]) {
                    computeBranch(b, band);
                    branch_m_[band] = zldsp::filter::dbToGain(static_cast<double>(static_gain)) - 1.0;
                    branchToDB(band, branch_m_[band], base_[band]);
                    if (b.dynamic_on) {
                        branchToDB(band, zldsp::filter::dbToGain(
                                       static_cast<double>(scaledGain(b.target_gain, gain_scale_))) - 1.0, target_[band]);
                    }
                } else {
                    computeMagnitude(b, band, static_gain, base_[band]);
                    if (b.dynamic_on) {
                        computeMagnitude(b, band, scaledGain(b.target_gain, gain_scale_), target_[band]);
                    }
                }
                if (!was_active_[band]) {
                    // a new band grows out of the flat line
                    std::fill(display_[band].begin(), display_[band].end(), 0.f);
                    was_active_[band] = true;
                }
                resolved_[band] = base_[band];
                resolved_m_[band] = branch_m_[band];
                targets_changed = true;
            }
            // the live dynamic response, as the engine currently applies it
            dynamic_delta_[band] = 0.f;
            if (b.dynamic_on && !b.dynamic_bypass && b.isOn()) {
                const auto current = static_cast<float>(controller.getCurrentGain(band));
                if (std::abs(current - static_gain) > 1e-3f) {
                    if (is_branch_[band]) {
                        resolved_m_[band] = zldsp::filter::dbToGain(static_cast<double>(current)) - 1.0;
                        branchToDB(band, resolved_m_[band], resolved_[band]);
                    } else {
                        computeMagnitude(b, band, current, resolved_[band]);
                    }
                    dynamic_delta_[band] = current - static_gain;
                } else {
                    resolved_[band] = base_[band];
                    resolved_m_[band] = branch_m_[band];
                }
                targets_changed = true;
            }
        }

        if (!targets_changed && !animating_) {
            return false;
        }
        if (targets_changed) {
            computeSums(bridge);
        }

        // glide the displayed curves towards their targets
        const auto alpha = snap || InteractionSettings::reduce_motion ? 1.f : static_cast<float>(1.0 - std::exp(-dt * 2.0 * std::numbers::pi * 4.5));
        float max_diff = 0.f;
        auto glide = [&](std::vector<float>& disp, const std::vector<float>& to) {
            for (size_t i = 0; i < disp.size(); ++i) {
                const auto d = to[i] - disp[i];
                disp[i] += d * alpha;
                max_diff = std::max(max_diff, std::abs(d));
            }
        };
        for (size_t band = 0; band < kBandNum; ++band) {
            if (bridge.band(band).isActive()) {
                // live dynamics are already smoothed by the engine
                if (dynamic_delta_[band] != 0.f) {
                    display_[band] = resolved_[band];
                } else {
                    glide(display_[band], resolved_[band]);
                }
            }
        }
        for (size_t lr = 0; lr < kNumStereo; ++lr) {
            glide(sum_display_[lr], sum_target_[lr]);
        }
        animating_ = !snap && max_diff > .01f;
        if (!animating_ && !snap) {
            for (size_t band = 0; band < kBandNum; ++band) {
                display_[band] = resolved_[band];
            }
            for (size_t lr = 0; lr < kNumStereo; ++lr) {
                sum_display_[lr] = sum_target_[lr];
            }
        }
        return true;
    }
}
