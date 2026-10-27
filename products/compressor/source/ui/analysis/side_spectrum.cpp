// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "side_spectrum.hpp"

namespace altvane::compressor {
    namespace {
        // as upstream's analyzer: 4.5 dB per octave of tilt, a fall of 0.15 s to −72 dB
        constexpr double kTilt = 4.5;
        constexpr float kDecaySeconds = .15f;
    }

    SideSpectrum::SideSpectrum(PluginProcessor& p) : p_ref_(p) {
        receiver_.setON(true);
        p_ref_.getEqualizeController().setFFTAnalyzerON(false);
    }

    void SideSpectrum::setOn(const bool on) {
        if (on != on_) {
            on_ = on;
            p_ref_.getEqualizeController().setFFTAnalyzerON(on);
        }
    }

    bool SideSpectrum::update(const float refresh_rate) {
        if (!on_) {
            return false;
        }
        auto& sender = p_ref_.getEqualizeController().getFFTAnalyzerSender();
        if (!sender.getLock().try_lock()) {
            return false;
        }
        const auto sample_rate = sender.getSampleRate();
        if (std::abs(sample_rate - sample_rate_) > .1) {
            sample_rate_ = sample_rate;
            const auto order = sample_rate <= 50000.0 ? 12 : sample_rate <= 100000.0 ? 13 : sample_rate <= 200000.0
                                                                                              ? 14
                                                                                              : 15;
            processor_.prepare(order);
            const auto fft_size = processor_.getFFTSize();
            history_size_ = static_cast<int>(fft_size);
            receiver_.prepare(2, fft_size);
            smoother_.prepare(fft_size);
            smoother_.setSmooth(.5, sample_rate, zldsp::analyzer::SpectrumSmoother::SmoothMethod::kERB);
            spectrum_.assign(fft_size / 2 + 1, -120.f);
            tilter_.prepareSpectrum(spectrum_.size());
            tilter_.setTiltSlope(sample_rate, kTilt);
            decayer_.prepareSpectrum(spectrum_.size());
            bin_width_ = sample_rate / static_cast<double>(fft_size);
            refresh_rate_ = 0.f;
        }
        auto& fifo = sender.getAbstractFIFO();
        auto num_read = fifo.getNumReady() / 4 * 3;
        if (num_read > history_size_) {
            (void) fifo.prepareToRead(num_read - history_size_);
            fifo.finishRead(num_read - history_size_);
            num_read = history_size_;
        }
        const auto range = fifo.prepareToRead(num_read);
        receiver_.pull(range, sender.getSampleFIFOs()[0]);
        fifo.finishRead(num_read);
        sender.getLock().unlock();

        if (std::abs(refresh_rate - refresh_rate_) > 1.f) {
            refresh_rate_ = refresh_rate;
            decayer_.setDecaySpeed(refresh_rate, -72.f, kDecaySeconds);
        }
        receiver_.forward(processor_, zldsp::analyzer::StereoType::kStereoLR, spectrum_);
        smoother_.smooth(spectrum_);
        zldsp::vector::sqr_mag_to_db(spectrum_.data(), spectrum_.size());
        tilter_.tilt(std::span{spectrum_.data(), spectrum_.size()});
        decayer_.decay(std::span{spectrum_.data(), spectrum_.size()});
        return true;
    }
}
