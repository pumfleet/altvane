// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "engine.hpp"

#include <bit>
#include <complex>
#include <numbers>

namespace altvane::deesser::dsp {
    namespace {
        void storeMax(std::atomic<float>& peak, const float value) {
            auto current = peak.load(std::memory_order::relaxed);
            while (value > current && !peak.compare_exchange_weak(current, value, std::memory_order::relaxed)) {
            }
        }

        // the bell's upper edge when the band is open
        constexpr double kOpenBellTop = 16000.0;
        // the shelf's damping (Butterworth)
        constexpr float kShelfK = 1.4142135f;
    }

    // ---------------------------------------------------------------- DelayLine

    void Engine::DelayLine::prepare(const int max_delay) {
        const auto size = std::bit_ceil(static_cast<size_t>(max_delay) + 2);
        buffer_.assign(size, 0.f);
        mask_ = size - 1;
        write_ = 0;
    }

    void Engine::DelayLine::reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.f);
        write_ = 0;
    }

    // ---------------------------------------------------------------- Engine

    Engine::Engine() :
        history_(std::make_unique<SpscRing<HistoryFrame, kHistoryCapacity>>()),
        analysis_(std::make_unique<SpscRing<AnalysisSample, kAnalysisCapacity>>()),
        events_(std::make_unique<SpscRing<float, kEventCapacity>>()) {
    }

    int Engine::lookaheadSamples(const float ms, const double sample_rate) {
        return static_cast<int>(std::lround(std::clamp(ms, 0.f, kMaxLookaheadMs) * .001 * sample_rate));
    }

    float Engine::responseDb(const Controls& c, const double sample_rate, const float hz, const float reduction_db) {
        if (reduction_db <= 0.f) {
            return 0.f;
        }
        if (c.processing == Processing::kWide) {
            return -reduction_db;
        }
        // the band as updateControlPeriod settles on it
        const auto top = .45 * sample_rate;
        const auto lo = std::clamp(static_cast<double>(c.band_low), 20.0, top / 1.3);
        const auto hi = std::clamp(static_cast<double>(c.band_high), lo * 1.26, top);
        const auto warp = [sample_rate](const double f) {
            return std::tan(std::numbers::pi * std::clamp(f, 1.0, .4999 * sample_rate) / sample_rate);
        };
        // a TPT state-variable filter is its analog prototype through the bilinear transform: evaluate the
        // prototype at the prewarped frequency, with the prewarped g of each filter
        using C = std::complex<double>;
        const C s{0.0, warp(hz)};
        const auto g = std::pow(10.0, -reduction_db / 20.0);
        C h;
        if (c.shape == Shape::kShelf) {
            const auto k = static_cast<double>(kShelfK);
            const auto wp = warp(std::min(lo, top)) * std::pow(g, .25);
            h = (wp * wp + k * std::sqrt(g) * wp * s + g * s * s) / (s * s + k * wp * s + wp * wp);
        } else {
            const auto bell_hi = std::max(c.band_open ? std::min(kOpenBellTop, top) : hi, lo * 1.26);
            const auto centre = std::sqrt(lo * bell_hi);
            const auto w0 = warp(std::min(centre, top));
            const auto k = (bell_hi - lo) / centre;
            h = (s * s + g * k * w0 * s + w0 * w0) / (s * s + k * w0 * s + w0 * w0);
        }
        return static_cast<float>(20.0 * std::log10(std::abs(h)));
    }

    void Engine::prepare(const double sample_rate, int) {
        sample_rate_ = sample_rate;
        const auto max_lookahead = lookaheadSamples(kMaxLookaheadMs, sample_rate);
        for (size_t c = 0; c < 2; ++c) {
            detectors_[c].prepare(sample_rate);
            computers_[c].prepare(sample_rate, max_lookahead);
            audio_delay_[c].prepare(max_lookahead);
            band_delay_[c].prepare(max_lookahead);
        }
        wide_.reset(sample_rate, .02);
        shelf_weight_.reset(sample_rate, .02);
        bell_weight_.reset(sample_rate, .02);
        normal_.reset(sample_rate, .015);
        trigger_.reset(sample_rate, .015);
        delta_.reset(sample_rate, .015);
        bypass_.reset(sample_rate, .05);
        output_.reset(sample_rate, .03);
        fade_length_ = std::max(1, static_cast<int>(.010 * sample_rate));
        freq_smooth_ = onePole(sample_rate / kControlInterval, .03);
        history_interval_ = std::max(1, static_cast<int>(std::lround(sample_rate / kHistoryRate)));
        // the hold falls 3 dB a second
        hold_decay_ = static_cast<float>(std::pow(10.0, -3.0 / 20.0 / sample_rate));
        event_min_length_ = static_cast<int>(.008 * sample_rate);
        event_gap_ = static_cast<int>(.010 * sample_rate);
        reset();
    }

    void Engine::reset() {
        for (size_t c = 0; c < 2; ++c) {
            detectors_[c].reset();
            computers_[c].reset();
            shelf_[c].reset();
            bell_[c].reset();
            audio_delay_[c].reset();
            band_delay_[c].reset();
        }
        lookahead_ = lookaheadSamples(controls_.lookahead_ms, sample_rate_);
        next_lookahead_ = lookahead_;
        layout_ = -1;
        fading_ = false;
        fade_pos_ = 0;
        control_count_ = 0;
        updateControlPeriod(true);
        history_count_ = 0;
        history_peak_ = {};
        history_in_ = history_out_ = history_band_ = 0.f;
        band_hold_ = 0.f;
        in_event_ = false;
    }

    void Engine::clearFeeds() {
        history_->clear();
        analysis_->clear();
        events_->clear();
    }

    void Engine::updateControlPeriod(const bool snap) {
        const auto& c = controls_;
        const auto top = .45 * sample_rate_;
        const auto low = std::clamp(static_cast<double>(c.band_low), 20.0, top / 1.3);
        const auto high = std::clamp(static_cast<double>(c.band_high), low * 1.26, top);
        if (snap) {
            low_log_ = static_cast<float>(std::log(low));
            high_log_ = static_cast<float>(std::log(high));
        } else {
            low_log_ += (static_cast<float>(std::log(low)) - low_log_) * freq_smooth_;
            high_log_ += (static_cast<float>(std::log(high)) - high_log_) * freq_smooth_;
        }
        const auto lo = std::exp(static_cast<double>(low_log_));
        const auto hi = std::exp(static_cast<double>(high_log_));
        detection_.hp1 = SVFCoeffs::make(lo, kButterworth4Q1, sample_rate_);
        detection_.hp2 = SVFCoeffs::make(lo, kButterworth4Q2, sample_rate_);
        detection_.open = c.band_open;
        if (!c.band_open) {
            detection_.lp1 = SVFCoeffs::make(hi, kButterworth4Q1, sample_rate_);
            detection_.lp2 = SVFCoeffs::make(hi, kButterworth4Q2, sample_rate_);
        }
        detection_.body = SVFCoeffs::make(lo, kButterworth2Q, sample_rate_);
        shelf_g_ = static_cast<float>(std::tan(std::numbers::pi * std::min(lo, top) / sample_rate_));
        shelf_coeffs_ = SVFCoeffs::fromG(shelf_g_, kShelfK);
        // the bell covers the band: centred on it (geometrically), its -3 dB edges at the band edges
        const auto bell_hi = std::max(c.band_open ? std::min(kOpenBellTop, top) : hi, lo * 1.26);
        const auto centre = std::sqrt(lo * bell_hi);
        bell_coeffs_ = SVFCoeffs::make(centre, centre / (bell_hi - lo), sample_rate_);

        // look-ahead: fade to a new delay, one change at a time
        const auto lookahead = lookaheadSamples(c.lookahead_ms, sample_rate_);
        if (!fading_ && lookahead != lookahead_) {
            next_lookahead_ = lookahead;
            fading_ = true;
            fade_pos_ = 0;
        }
        for (auto& computer : computers_) {
            computer.setTiming(fading_ ? next_lookahead_ : lookahead_);
        }

        knee_foot_ = dbToGain(c.threshold_db - .5f * GainComputer::kKneeDb);

        const auto split = c.processing == Processing::kSplit;
        auto set = [snap](Ramp& ramp, const float target) {
            ramp.setTarget(target);
            if (snap) {
                ramp.snap(target);
            }
        };
        set(wide_, split ? 0.f : 1.f);
        set(shelf_weight_, split && c.shape == Shape::kShelf ? 1.f : 0.f);
        set(bell_weight_, split && c.shape == Shape::kBell ? 1.f : 0.f);
        set(normal_, c.listen == Listen::kOff ? 1.f : 0.f);
        set(trigger_, c.listen == Listen::kTrigger ? 1.f : 0.f);
        set(delta_, c.listen == Listen::kDelta ? 1.f : 0.f);
        set(bypass_, c.bypass ? 1.f : 0.f);
        set(output_, dbToGain(c.output_db));
    }

    float Engine::readDelayed(const DelayLine& line) const {
        if (!fading_) {
            return line.read(lookahead_);
        }
        const auto t = static_cast<float>(fade_pos_) / static_cast<float>(fade_length_);
        return line.read(lookahead_) * (1.f - t) + line.read(next_lookahead_) * t;
    }

    void Engine::trackEvent(const float level, const bool sibilant_now) {
        band_hold_ = std::max(level, band_hold_ * hold_decay_);
        // within 30 dB of the hold, and above -80 dB
        const auto sibilant = sibilant_now && level > std::max(band_hold_ * .0316228f, 1e-4f);
        if (sibilant) {
            if (!in_event_) {
                in_event_ = true;
                event_peak_ = 0.f;
                event_length_ = 0;
            }
            event_peak_ = std::max(event_peak_, level);
            ++event_length_;
            event_quiet_ = 0;
        } else if (in_event_ && ++event_quiet_ > event_gap_) {
            in_event_ = false;
            if (event_length_ >= event_min_length_ && feeds_active_.load(std::memory_order::relaxed)) {
                events_->push(gainToDb(event_peak_));
            }
        }
    }

    void Engine::process(float* const* io, const int channels, const float* const* side, const int side_channels,
                         const int n) {
        const auto& c = controls_;
        const auto stereo = channels > 1;
        const auto ms = stereo && c.stereo != StereoMode::kStereo;
        // detection (and processing) channels: both in stereo, one for mono, mid or side
        const auto detection_channels = stereo && !ms ? 2 : 1;
        if (const auto layout = stereo ? static_cast<int>(c.stereo) : -2; layout != layout_) {
            // channels that now carry something else (left becomes mid, an idle right comes back) start afresh,
            // rather than with the reduction they had
            if (layout_ != -1) {
                for (size_t ch = 0; ch < 2; ++ch) {
                    detectors_[ch].reset();
                    computers_[ch].reset();
                    shelf_[ch].reset();
                    bell_[ch].reset();
                }
            }
            layout_ = layout;
        }
        const auto feeds = feeds_active_.load(std::memory_order::relaxed);
        const auto link = std::clamp(c.link, 0.f, 1.f);
        float block_peak = 0.f;

        for (int i = 0; i < n; ++i) {
            if (control_count_ == 0) {
                updateControlPeriod(false);
            }
            control_count_ = (control_count_ + 1) % kControlInterval;

            const auto in0 = io[0][i];
            const auto in1 = stereo ? io[1][i] : in0;
            auto d0 = in0, d1 = in1;
            const auto mono_key = side != nullptr && side_channels == 1;
            if (side != nullptr && side_channels > 0) {
                d0 = side[0][i];
                d1 = side_channels > 1 ? side[1][i] : d0;
                if (!stereo && side_channels > 1) {
                    // a mono track listens to both sides of a stereo side chain
                    d0 = .5f * (d0 + d1);
                }
            }
            std::array<float, 2> det{d0, d1};
            if (ms && !mono_key) {
                // a mono key has no sides to take apart: it keys mid and side alike
                det[0] = c.stereo == StereoMode::kMid ? .5f * (d0 + d1) : .5f * (d0 - d1);
            }

            // detection and gain reduction
            std::array<Detection, 2> found;
            std::array<float, 2> wanted{0.f, 0.f};
            for (int p = 0; p < detection_channels; ++p) {
                const auto pi = static_cast<size_t>(p);
                found[pi] = detectors_[pi].process(det[pi], detection_, c.mode);
                // decibels only near and above the threshold: below the knee's foot nothing is wanted
                if (found[pi].level > knee_foot_) {
                    wanted[pi] = GainComputer::target(gainToDb(found[pi].level) - c.threshold_db, found[pi].sibilance(),
                                                      c.range_db);
                }
            }
            if (detection_channels == 2) {
                const auto most = std::max(wanted[0], wanted[1]);
                wanted[0] = link * most + (1.f - link) * wanted[0];
                wanted[1] = link * most + (1.f - link) * wanted[1];
            }
            std::array<float, 2> reduction{0.f, 0.f};
            for (int p = 0; p < detection_channels; ++p) {
                const auto pi = static_cast<size_t>(p);
                reduction[pi] = computers_[pi].process(wanted[pi]);
                band_delay_[pi].push(found[pi].band);
            }

            // the delayed audio, encoded for processing
            audio_delay_[0].push(in0);
            audio_delay_[1].push(in1);
            const std::array<float, 2> dry{readDelayed(audio_delay_[0]), stereo ? readDelayed(audio_delay_[1]) : 0.f};
            const auto mid = .5f * (dry[0] + dry[1]);
            const auto sid = .5f * (dry[0] - dry[1]);
            std::array<float, 2> proc{dry[0], dry[1]};
            if (ms) {
                proc[0] = c.stereo == StereoMode::kMid ? mid : sid;
            }

            // reduction, mixed in
            const auto w_wide = wide_.next();
            const auto w_shelf = shelf_weight_.next();
            const auto w_bell = bell_weight_.next();
            std::array<float, 2> wet{proc[0], proc[1]};
            for (int p = 0; p < detection_channels; ++p) {
                const auto pi = static_cast<size_t>(p);
                const auto x = proc[pi];
                const auto bell = bell_coeffs_.k * bell_[pi].process(x, bell_coeffs_).bp;
                if (reduction[pi] > 0.f) {
                    const auto root = dbToGain(-.5f * reduction[pi]);
                    const auto g = root * root;
                    // the shelf: lp + k sqrt(g) bp + g hp, a second-order high shelf down to g, with its poles
                    // at f g^(1/4) and its zeros at f g^(-1/4) so its midpoint stays on the band's low edge
                    const auto shelf = shelf_[pi].process(x, SVFCoeffs::fromG(shelf_g_ * std::sqrt(root), kShelfK));
                    // written as the input plus what changes, so nothing changes when g is 1; the bell is the
                    // input less (1 - g) of its unity-peak band-pass
                    wet[pi] = x + (g - 1.f) * (w_wide * x + w_shelf * shelf.hp + w_bell * bell)
                              + (root - 1.f) * w_shelf * kShelfK * shelf.bp;
                } else {
                    shelf_[pi].process(x, shelf_coeffs_);
                }
            }

            std::array<float, 2> out{wet[0], wet[1]};
            std::array<float, 2> trig{readDelayed(band_delay_[0]), readDelayed(band_delay_[1])};
            if (ms) {
                if (c.stereo == StereoMode::kMid) {
                    out = {wet[0] + sid, wet[0] - sid};
                } else {
                    out = {mid + wet[0], mid - wet[0]};
                }
                trig[1] = trig[0];
            } else if (!stereo) {
                trig[1] = trig[0];
            }

            // listen, output gain, bypass
            const auto w_normal = normal_.next();
            const auto w_trigger = trigger_.next();
            const auto w_delta = delta_.next();
            const auto bypass = bypass_.next();
            const auto gain = output_.next();
            float out_peak = 0.f;
            std::array<float, 2> final{0.f, 0.f};
            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch) {
                const auto chi = static_cast<size_t>(ch);
                const auto o = w_normal * out[chi] + w_trigger * trig[chi] + w_delta * (dry[chi] - out[chi]);
                final[chi] = (1.f - bypass) * gain * o + bypass * dry[chi];
                io[ch][i] = final[chi];
                out_peak = std::max(out_peak, std::abs(final[chi]));
            }

            if (fading_ && ++fade_pos_ >= fade_length_) {
                lookahead_ = next_lookahead_;
                fading_ = false;
            }

            // what the editor sees
            const auto most_reduction = std::max(reduction[0], reduction[1]);
            block_peak = std::max(block_peak, most_reduction);
            const auto level = std::max(found[0].level, detection_channels == 2 ? found[1].level : 0.f);
            const auto sibilant = found[0].isSibilant() || (detection_channels == 2 && found[1].isSibilant());
            trackEvent(level, sibilant);
            if (feeds) {
                history_in_ = std::max({history_in_, std::abs(dry[0]), std::abs(dry[1])});
                history_out_ = std::max(history_out_, out_peak);
                // the band's envelope, as the threshold sees it
                history_band_ = std::max(history_band_, level);
                history_peak_.reduction_db = std::max(history_peak_.reduction_db, most_reduction);
                history_peak_.sibilance = std::max(history_peak_.sibilance, sibilant ? 1.f : 0.f);
                if (++history_count_ >= history_interval_) {
                    history_peak_.input_db = gainToDb(history_in_);
                    history_peak_.output_db = gainToDb(history_out_);
                    history_peak_.band_db = gainToDb(history_band_);
                    history_->push(history_peak_);
                    history_peak_ = {};
                    history_in_ = history_out_ = history_band_ = 0.f;
                    history_count_ = 0;
                }
                const auto mono_side = detection_channels == 2 ? .5f * (det[0] + det[1]) : det[0];
                const auto mono_out = stereo ? .5f * (final[0] + final[1]) : final[0];
                analysis_->push({mono_side, mono_out, in_event_ ? 1.f : 0.f});
            }
        }
        storeMax(reduction_peak_, block_peak);
    }
}
