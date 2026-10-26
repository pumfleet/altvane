// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "spectrum_worker.hpp"

#include <algorithm>
#include "altvane_ui/util/path_minimizer.hpp"
#include "altvane_ui/util/gradient_minimizer.hpp"
#include "../../zlp/sample_rate_helper.hpp"
#include "../../dsp/analyzer/mag_analyzer/mag_receiver_base.hpp"

namespace altvane::eq {
    SpectrumWorker::SpectrumWorker(PluginProcessor& p, MatchEngine* match) :
        Thread("contour_spectrum"),
        p_ref_(p),
        match_(match),
        pre_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTPreON::kID)),
        post_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTPostON::kID)),
        side_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTSideON::kID)),
        stereo_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTStereo::kID)),
        freeze_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTFreezeON::kID)),
        top_db_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTTopDB::kID)),
        min_db_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTMinDB::kID)),
        speed_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTSpeed::kID)),
        tilt_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTTilt::kID)),
        smooth_oct_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTSmoothOCTValue::kID)),
        smooth_erb_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTSmoothERBValue::kID)),
        smooth_type_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PFFTSmoothType::kID)),
        collision_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PCollisionON::kID)),
        collision_strength_ref_(*p.parameters_NA_.getRawParameterValue(zlstate::PCollisionStrength::kID)) {
        for (auto& peaks : peaks_.getBuffer()) {
            peaks.reserve(kMaxPeaks);
        }
        for (auto& buffered : paths_) {
            for (auto& paths : buffered.getBuffer()) {
                paths.fill.preallocateSpace(3 * 1024);
                paths.line.preallocateSpace(3 * 1024);
            }
        }
        for (auto& receiver : receivers_) {
            receiver.setON(true);
        }
        for (auto& peak : out_peaks_) {
            peak.store(0.f);
        }
    }

    SpectrumWorker::~SpectrumWorker() {
        stop();
    }

    void SpectrumWorker::start() {
        if (!isThreadRunning()) {
            startThread(juce::Thread::Priority::low);
        }
    }

    void SpectrumWorker::stop() {
        signalThreadShouldExit();
        notify();
        stopThread(1000);
    }

    void SpectrumWorker::setBounds(const juce::Rectangle<float> graph_bounds) {
        bound_x_.store(graph_bounds.getX(), std::memory_order::relaxed);
        bound_y_.store(graph_bounds.getY(), std::memory_order::relaxed);
        bound_w_.store(std::max(graph_bounds.getWidth(), 1.f), std::memory_order::relaxed);
        bound_h_.store(std::max(graph_bounds.getHeight(), 1.f), std::memory_order::relaxed);
        to_update_geometry_.signal();
    }

    void SpectrumWorker::setRefreshRate(const double hz) {
        const auto r = static_cast<float>(std::clamp(hz, 15.0, 240.0));
        if (std::abs(r - refresh_rate_.load(std::memory_order::relaxed)) > 2.f) {
            refresh_rate_.store(r, std::memory_order::relaxed);
            to_update_decay_.signal();
        }
    }

    bool SpectrumWorker::pull() {
        bool changed = false;
        source_on_ = {
            pre_ref_.load(std::memory_order::relaxed) > .5f,
            post_ref_.load(std::memory_order::relaxed) > .5f,
            side_ref_.load(std::memory_order::relaxed) > .5f
        };
        for (size_t i = 0; i < kNumSources; ++i) {
            if (source_on_[i]) {
                changed = paths_[i].pull() || changed;
            }
        }
        peaks_.pull();
        collision_on_ = collision_ref_.load(std::memory_order::relaxed) > .5f
            && source_on_[kPost] && (source_on_[kSide] || source_on_[kPre]);
        if (collision_on_) {
            changed = collision_.pull() || changed;
        }
        return changed;
    }

    float SpectrumWorker::takeOutputPeak(const size_t channel) {
        const auto peak = out_peaks_[channel].exchange(0.f, std::memory_order::relaxed);
        return juce::Decibels::gainToDecibels(peak, -120.f);
    }

    void SpectrumWorker::run() {
        while (!threadShouldExit()) {
            wait(-1);
            if (threadShouldExit()) {
                break;
            }
            runOnce();
        }
    }

    void SpectrumWorker::prepareResolution(const double sample_rate) {
        const auto middle_order = static_cast<int>(zlp::getScaledOrder(sample_rate, 12));
        const std::array fft_orders{middle_order + 2, middle_order, middle_order - 2};
        for (size_t i = 0; i < kNumResolutions; ++i) {
            processors_[i].prepare(fft_orders[i]);
        }
        // equalise the expected white-noise power of the normalised Hann windows
        const auto reference_power = processors_[kMiddle].getWindowSqrSum();
        for (size_t i = 0; i < kNumResolutions; ++i) {
            noise_power_scales_[i] = static_cast<float>(reference_power / processors_[i].getWindowSqrSum());
            smoothers_[i].prepare(processors_[i].getFFTSize());
        }
        history_size_ = static_cast<int>(processors_[kLow].getFFTSize());
        for (auto& receiver : receivers_) {
            receiver.prepare(2, static_cast<size_t>(history_size_));
        }
        frequencies_ = zldsp::analyzer::SpectrumBlender::createFrequencyGrid(
            processors_[kLow].getFFTSize(), processors_[kMiddle].getFFTSize(), processors_[kHigh].getFFTSize(),
            sample_rate);
        tilter_.prepareSpectrum(frequencies_.size());
        for (auto& decayer : decayers_) {
            decayer.prepareSpectrum(frequencies_.size());
        }
        xs_.resize(frequencies_.size());
        ys_.resize(frequencies_.size());
        collision_ps_.assign(frequencies_.size(), 0.f);
        collision_final_ps_.assign(frequencies_.size(), 0.f);
        for (size_t s = 0; s < kNumSources; ++s) {
            spectra_[s].resize(frequencies_.size());
            for (size_t r = 0; r < kNumResolutions; ++r) {
                res_spectra_[s][r].resize(processors_[r].getFFTSize() / 2 + 1);
            }
        }
        if (match_ != nullptr) {
            match_->prepare(sample_rate, static_cast<size_t>(history_size_));
        }
        c_tilt_idx_ = -1;
        c_smooth_type_idx_ = -1;
        to_update_decay_.signal();
        to_update_geometry_.signal();
    }

    void SpectrumWorker::runOnce() {
        juce::ScopedNoDenormals no_denormals;
        const std::array<bool, kNumSources> is_on{
            pre_ref_.load(std::memory_order::relaxed) > .5f,
            post_ref_.load(std::memory_order::relaxed) > .5f,
            side_ref_.load(std::memory_order::relaxed) > .5f
        };
        auto& sender{p_ref_.getController().getAnalyzerSender()};
        if (!sender.getLock().try_lock()) {
            return;
        }
        const auto sample_rate = sender.getSampleRate();
        if (std::abs(c_sample_rate_ - sample_rate) > 0.1) {
            c_sample_rate_ = sample_rate;
            prepareResolution(sample_rate);
        }
        // pull samples; meter everything consumed before trimming to the FFT history
        auto& fifo{sender.getAbstractFIFO()};
        const auto num_ready = fifo.getNumReady();
        auto num_read = num_ready / 4 * 3;
        if (num_read == 0) {
            num_read = num_ready;
        }
        {
            const auto meter_range = fifo.prepareToRead(num_read);
            const auto& out_fifo = sender.getSampleFIFOs()[kPost];
            for (size_t chan = 0; chan < std::min<size_t>(2, out_fifo.size()); ++chan) {
                const auto peak = zldsp::analyzer::MagAnalyzerOps::calculatePeak(meter_range, out_fifo[chan]);
                auto prev = out_peaks_[chan].load(std::memory_order::relaxed);
                while (peak > prev && !out_peaks_[chan].compare_exchange_weak(prev, peak)) {
                }
            }
        }
        if (num_read > history_size_) {
            (void)fifo.prepareToRead(num_read - history_size_);
            fifo.finishRead(num_read - history_size_);
            num_read = history_size_;
        }
        const auto range = fifo.prepareToRead(num_read);
        for (size_t i = 0; i < kNumSources; ++i) {
            if (is_on[i]) {
                receivers_[i].pull(range, sender.getSampleFIFOs()[i]);
            }
        }
        const auto matching = match_ != nullptr && match_->isActive();
        if (matching) {
            match_->feed(range, sender.getSampleFIFOs()[kPre], sender.getSampleFIFOs()[kSide]);
        }
        fifo.finishRead(num_read);
        sender.getLock().unlock();
        if (matching) {
            match_->process();
        }
        if (threadShouldExit() || history_size_ <= 0) {
            return;
        }

        // analyzer settings
        const auto top_db = zlstate::PFFTTopDB::kDBs[static_cast<size_t>(std::round(
            top_db_ref_.load(std::memory_order::relaxed)))];
        const auto range_db = zlstate::PFFTMinDB::kDBs[static_cast<size_t>(std::round(
            min_db_ref_.load(std::memory_order::relaxed)))];
        if (std::abs(top_db - c_top_db_) > .1f || std::abs(range_db - c_range_db_) > .1f) {
            c_top_db_ = top_db;
            c_range_db_ = range_db;
            to_update_decay_.signal();
            to_update_geometry_.signal();
        }
        if (const auto tilt_idx = static_cast<int>(std::round(tilt_ref_.load(std::memory_order::relaxed)));
            tilt_idx != c_tilt_idx_) {
            c_tilt_idx_ = tilt_idx;
            tilter_.setTiltSlope(frequencies_, zlstate::PFFTTilt::kSlopes[static_cast<size_t>(tilt_idx)]);
        }
        if (const auto speed_idx = static_cast<int>(std::round(speed_ref_.load(std::memory_order::relaxed)));
            speed_idx != c_speed_idx_) {
            c_speed_idx_ = speed_idx;
            to_update_decay_.signal();
        }
        if (to_update_decay_.check()) {
            const auto speed = zlstate::PFFTSpeed::kSpeeds[static_cast<size_t>(c_speed_idx_)];
            for (auto& decayer : decayers_) {
                decayer.setDecaySpeed(refresh_rate_.load(std::memory_order::relaxed), c_top_db_ + c_range_db_,
                                      static_cast<float>(0.15 / speed));
            }
        }
        const auto oct_idx = static_cast<int>(std::round(smooth_oct_ref_.load(std::memory_order::relaxed)));
        const auto erb_idx = static_cast<int>(std::round(smooth_erb_ref_.load(std::memory_order::relaxed)));
        const auto type_idx = static_cast<int>(std::round(smooth_type_ref_.load(std::memory_order::relaxed)));
        if (oct_idx != c_smooth_oct_idx_ || erb_idx != c_smooth_erb_idx_ || type_idx != c_smooth_type_idx_) {
            c_smooth_oct_idx_ = oct_idx;
            c_smooth_erb_idx_ = erb_idx;
            c_smooth_type_idx_ = type_idx;
            for (auto& smoother : smoothers_) {
                if (type_idx == 0) {
                    smoother.setSmoothOCT(zlstate::PFFTSmoothOCTValue::kValues[static_cast<size_t>(oct_idx)]);
                } else {
                    smoother.setSmoothERB(sample_rate,
                                          zlstate::PFFTSmoothERBValue::kValues[static_cast<size_t>(erb_idx)]);
                }
            }
        }

        // geometry
        if (to_update_geometry_.check()) {
            const auto bx = bound_x_.load(std::memory_order::relaxed);
            const auto by = bound_y_.load(std::memory_order::relaxed);
            const auto bw = bound_w_.load(std::memory_order::relaxed);
            const auto bh = bound_h_.load(std::memory_order::relaxed);
            freq_scale_.setBounds(bx, bw);
            c_bottom_ = by + bh;
            first_point_ = 0;
            num_points_ = 0;
            const auto right = bx + bw;
            for (size_t i = 0; i < frequencies_.size(); ++i) {
                const auto f = i == 0 ? frequencies_[1] * .5f : frequencies_[i];
                xs_[i] = freq_scale_.freqToX(f);
                if (xs_[i] < bx) {
                    first_point_ = i;
                }
                if (xs_[i] > right) {
                    num_points_ = i + 1;
                    break;
                }
                num_points_ = i + 1;
            }
            // top of the analyzer range sits slightly below the graph top
            const auto top = by + bh * .04f;
            y_k_ = (c_bottom_ - top) / c_range_db_;
            y_b_ = top - c_top_db_ * y_k_;
        }
        if (num_points_ < first_point_ + 3) {
            return;
        }

        const auto stereo = static_cast<zldsp::analyzer::StereoType>(std::round(
            stereo_ref_.load(std::memory_order::relaxed)));
        const auto frozen = freeze_ref_.load(std::memory_order::relaxed) > .5f
            && mouse_over_.load(std::memory_order::relaxed);
        for (size_t i = 0; i < kNumSources; ++i) {
            if (!is_on[i]) {
                continue;
            }
            auto& spectrum{spectra_[i]};
            for (size_t r = 0; r < kNumResolutions; ++r) {
                auto& res_spectrum = res_spectra_[i][r];
                receivers_[i].forward(processors_[r], stereo, res_spectrum);
                zldsp::vector::multiply(res_spectrum.data(), noise_power_scales_[r], res_spectrum.size());
                smoothers_[r].smooth(res_spectrum);
            }
            zldsp::analyzer::SpectrumBlender::blend(spectrum, frequencies_,
                                                    res_spectra_[i][kLow], res_spectra_[i][kMiddle],
                                                    res_spectra_[i][kHigh], sample_rate);
            zldsp::vector::sqr_mag_to_db(spectrum.data(), spectrum.size());
            tilter_.tilt(std::span{spectrum.data(), spectrum.size()});
            decayers_[i].decay(std::span{spectrum.data(), spectrum.size()}, frozen);
            zldsp::vector::fma(ys_.data(), spectrum.data(), y_k_, y_b_, num_points_);

            auto& [fill, line]{paths_[i].getWriter()};
            line.clear();
            const auto floor = c_bottom_ + 4.f;
            PathMinimizer<4> minimizer{line};
            minimizer.startNewSubPath(xs_[first_point_], std::min(ys_[first_point_], floor));
            for (size_t j = first_point_ + 1; j < num_points_; ++j) {
                minimizer.lineTo(xs_[j], std::min(ys_[j], floor));
            }
            minimizer.finish();
            fill = line;
            fill.lineTo(xs_[num_points_ - 1], floor);
            fill.lineTo(xs_[first_point_], floor);
            fill.closeSubPath();
            if (threadShouldExit()) {
                return;
            }
        }
        for (size_t i = 0; i < kNumSources; ++i) {
            if (is_on[i]) {
                paths_[i].publish();
            }
        }
        // peaks for Spectrum Grab come from the output, or the input when the output is hidden
        if (is_on[kPost] || is_on[kPre]) {
            auto& peaks = peaks_.getWriter();
            findPeaks(spectra_[is_on[kPost] ? kPost : kPre], peaks);
            peaks_.publish();
        }
        if (collision_ref_.load(std::memory_order::relaxed) > .5f && is_on[kPost] && (is_on[kSide] || is_on[kPre])) {
            buildCollision(spectra_[kPost], spectra_[is_on[kSide] ? kSide : kPre]);
        }
    }

    void SpectrumWorker::findPeaks(const zldsp::vector::aligned_vector<float>& spectrum, std::vector<Peak>& peaks) const {
        peaks.clear();
        const auto floor_db = c_top_db_ + c_range_db_ + 9.f;
        constexpr float kSearch = 36.f;    // px each side to look for the surrounding valley
        constexpr float kProminence = 5.f; // dB above the higher valley
        for (size_t j = first_point_ + 1; j + 1 < num_points_; ++j) {
            const auto v = spectrum[j];
            if (v < floor_db || v < spectrum[j - 1] || v < spectrum[j + 1]) {
                continue;
            }
            float left_min = v, right_min = v;
            bool dominated = false;
            for (size_t k = j; k-- > first_point_ && xs_[j] - xs_[k] < kSearch;) {
                if (spectrum[k] > v) {
                    dominated = true;
                    break;
                }
                left_min = std::min(left_min, spectrum[k]);
            }
            for (size_t k = j + 1; !dominated && k < num_points_ && xs_[k] - xs_[j] < kSearch; ++k) {
                if (spectrum[k] > v) {
                    dominated = true;
                    break;
                }
                right_min = std::min(right_min, spectrum[k]);
            }
            if (dominated || v - std::max(left_min, right_min) < kProminence) {
                continue;
            }
            peaks.push_back({xs_[j], ys_[j], frequencies_[j], v});
        }
        if (peaks.size() > kMaxPeaks) {
            std::ranges::partial_sort(peaks, peaks.begin() + kMaxPeaks,
                                      [](const Peak& a, const Peak& b) { return a.db > b.db; });
            peaks.resize(kMaxPeaks);
        }
    }

    void SpectrumWorker::buildCollision(zldsp::vector::aligned_vector<float>& a,
                                        zldsp::vector::aligned_vector<float>& b) {
        const auto n = num_points_ - first_point_;
        zldsp::analyzer::SpectrumCollision<float>::createGradientPs(
            std::span{a.data() + first_point_, n},
            std::span{b.data() + first_point_, n},
            std::span{collision_ps_.data(), n}, std::span{collision_final_ps_.data(), n},
            collision_strength_ref_.load(std::memory_order::relaxed));
        const auto x0 = bound_x_.load(std::memory_order::relaxed);
        const auto w = bound_w_.load(std::memory_order::relaxed);
        auto& gradient = collision_.getWriter();
        gradient.clearColours();
        gradient.point1 = {x0, 0.f};
        gradient.point2 = {x0 + w, 0.f};
        gradient.isRadial = false;
        GradientMinimizer minimizer(gradient, juce::Colour{0xFFF0656A});
        minimizer.start(0.f, 0.f);
        for (size_t i = 0; i < n; ++i) {
            minimizer.addColour((xs_[first_point_ + i] - x0) / w, collision_final_ps_[i]);
        }
        minimizer.finish();
        collision_.publish();
    }
}
