// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "match_engine.hpp"

#include <algorithm>
#include <numbers>

#include "../../zlp/sample_rate_helper.hpp"
#include "../../dsp/chore/decibels.hpp"

namespace altvane::eq {
    namespace {
        double fftMax(const double sample_rate) {
            return sample_rate >= 40000.0 && sample_rate < 50000.0 ? 30000.0 : .5 * sample_rate;
        }
    }

    MatchEngine::MatchEngine(EngineBridge& bridge) : Thread("contour_match_fit"), bridge_(bridge) {
        for (auto& d : drawing_) {
            d.store(kNotDrawn, std::memory_order::relaxed);
        }
        for (auto& r : receivers_) {
            r.setON(true);
        }
    }

    MatchEngine::~MatchEngine() {
        on_state_changed = nullptr;
        alive_token_.reset();
        stopTimer();
        stopThread(4000);
        if (state_ != State::kOff) {
            cancel();
        }
    }

    // ---------------------------------------------------------------- worker thread

    void MatchEngine::prepare(const double sample_rate, const size_t history_size) {
        sample_rate_ = sample_rate;
        const auto order = static_cast<int>(zlp::getScaledOrder(sample_rate, 12));
        processor_.prepare(order);
        fft_size_ = processor_.getFFTSize();
        for (auto& r : receivers_) {
            r.prepare(2, std::max(history_size, fft_size_));
        }
        for (auto& a : accumulators_) {
            a.prepare(fft_size_);
        }
        smoother_.prepare(fft_size_);
        smoother_.setSmoothOCT(zlstate::PFFTSmoothOCTValue::kValues[2]);
        tilter_.prepare(fft_size_);
        tilter_.setTiltSlope(sample_rate, 4.5f);
        raw_freqs_.resize(fft_size_ / 2 + 1);
        raw_dbs_.resize(fft_size_ / 2 + 1);
        for (size_t i = 0; i < raw_freqs_.size(); ++i) {
            raw_freqs_[i] = static_cast<float>(static_cast<double>(i) * sample_rate / static_cast<double>(fft_size_));
        }
        interpolator_ = std::make_unique<zldsp::interpolation::SeqMakima<float>>(
            raw_freqs_.data(), raw_dbs_.data(), raw_freqs_.size(), 0.f, 0.f);
        // the evaluation grid is shared by every published frame
        const auto top = fftMax(sample_rate) - .1;
        for (auto& frame : frames_.getBuffer()) {
            for (size_t i = 0; i < kNumPoints; ++i) {
                frame.freqs[i] = static_cast<float>(10.0 * std::pow(top / 10.0, static_cast<double>(i) / (kNumPoints - 1)));
            }
        }
        to_reset_.signal();
        to_update_target_.signal();
    }

    void MatchEngine::feed(const zldsp::container::FIFORange range, const std::vector<std::vector<float>>& main_fifo,
                           const std::vector<std::vector<float>>& side_fifo) {
        if (fft_size_ == 0) {
            return;
        }
        receivers_[0].pull(range, main_fifo);
        if (target_.load(std::memory_order::relaxed) == Target::kSideChain) {
            receivers_[1].pull(range, side_fifo);
        }
        pending_samples_ += static_cast<size_t>(range.block_size1 + range.block_size2);
    }

    void MatchEngine::computeTarget() {
        const auto& freqs = frames_.getWriter().freqs;
        const auto target = target_.load(std::memory_order::relaxed);
        if (target == Target::kFile) {
            std::lock_guard lock{file_mutex_};
            if (file_freqs_.size() >= 2) {
                auto xs = file_freqs_;
                auto ys = file_dbs_;
                zldsp::interpolation::SeqMakima<float> interp{xs.data(), ys.data(), xs.size(), 0.f, 0.f};
                interp.prepare();
                auto f = freqs;
                interp.eval(f.data(), target_db_.data(), kNumPoints);
            } else {
                target_db_.fill(-30.f);
            }
            return;
        }
        if (target == Target::kFlat) {
            target_db_.fill(-30.f);
            return;
        }
        // natural and balance mirror the upstream reference curves
        std::array<float, kNumPoints> ws{}, a{}, b{}, c{};
        const auto scale = static_cast<float>(2.0 * std::numbers::pi / sample_rate_);
        for (size_t i = 0; i < kNumPoints; ++i) {
            ws[i] = freqs[i] * scale;
        }
        zldsp::filter::Ideal<float, 2> hpf, lpf, hsf;
        for (auto* f : {&hpf, &lpf, &hsf}) {
            f->prepare(sample_rate_);
        }
        const auto natural = target == Target::kNatural;
        hpf.forceUpdate({zldsp::filter::kHighPass, 2, natural ? 40.0 : 35.0, 0.0, .707});
        lpf.forceUpdate({zldsp::filter::kLowPass, 2, natural ? 16000.0 : 17500.0, 0.0, .707});
        hsf.forceUpdate({zldsp::filter::kHighShelf, 2, 6600.0, natural ? 0.0 : 1.998, .707});
        hpf.updateMagnitudeSquare(ws, a);
        lpf.updateMagnitudeSquare(ws, b);
        hsf.updateMagnitudeSquare(ws, c);
        for (size_t i = 0; i < kNumPoints; ++i) {
            auto db = static_cast<float>(zldsp::chore::squareGainToDecibels(static_cast<double>(a[i] * b[i] * c[i]))) - 30.f;
            if (natural) {
                db += static_cast<float>(std::log2(freqs[i] / 1000.0) * 1.5);
            }
            target_db_[i] = db;
        }
    }

    void MatchEngine::process() {
        if (fft_size_ == 0) {
            return;
        }
        if (to_reset_.check()) {
            for (auto& a : accumulators_) {
                a.reset();
            }
            learned_samples_ = 0.0;
            pending_samples_ = 0;
        }
        auto& frame = frames_.getWriter();
        if (to_update_target_.check()) {
            computeTarget();
        }
        auto analyse = [&](const size_t idx, std::array<float, kNumPoints>& out) {
            receivers_[idx].forward(zldsp::analyzer::StereoType::kStereo);
            auto& spectrum = receivers_[idx].getAbsSqrFFTBuffer();
            accumulators_[idx].process(spectrum);
            smoother_.smooth(spectrum);
            zldsp::vector::sqr_mag_to_db(raw_dbs_.data(), spectrum.data(), spectrum.size());
            tilter_.tilt(std::span{raw_dbs_.data(), raw_dbs_.size()});
            interpolator_->prepare();
            auto f = frame.freqs;
            interpolator_->eval(f.data(), out.data(), kNumPoints);
        };
        analyse(0, frame.source);
        if (target_.load(std::memory_order::relaxed) == Target::kSideChain) {
            analyse(1, frame.target);
        } else {
            frame.target = target_db_;
        }
        learned_samples_ += static_cast<double>(pending_samples_);
        pending_samples_ = 0;
        frame.seconds = static_cast<float>(learned_samples_ / sample_rate_);

        // correction = what the EQ must do to move the source onto the reference
        double sum = 0.0;
        size_t count = 0;
        for (size_t i = 0; i < kNumPoints; ++i) {
            if (frame.freqs[i] >= 20.f && frame.freqs[i] <= 20000.f) {
                sum += static_cast<double>(frame.source[i] - frame.target[i]);
                ++count;
            }
        }
        const auto mean = count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.f;
        const auto amount = amount_.load(std::memory_order::relaxed);
        const auto tilt = tilt_.load(std::memory_order::relaxed);
        const auto limit = limit_.load(std::memory_order::relaxed);
        for (size_t i = 0; i < kNumPoints; ++i) {
            auto c = -(frame.source[i] - frame.target[i] - mean) * amount
                + tilt * static_cast<float>(std::log2(frame.freqs[i] / 1000.0));
            if (const auto d = drawing_[i].load(std::memory_order::relaxed); d > -100.f) {
                c = d;
            }
            frame.correction[i] = std::clamp(c, -limit, limit);
        }
        frame.valid = learned_samples_ > 0.0;
        frames_.publish();
    }

    // ---------------------------------------------------------------- message thread

    void MatchEngine::setState(const State state) {
        if (state_ != state) {
            state_ = state;
            if (on_state_changed) {
                on_state_changed();
            }
        }
    }

    void MatchEngine::start() {
        if (state_ != State::kOff) {
            return;
        }
        clearDrawing();
        fitted_.clear();
        num_applied_ = 0;
        suggested_ = 0;
        to_reset_.signal();
        active_.store(true, std::memory_order::release);
        setState(State::kLearning);
    }

    void MatchEngine::relearn() {
        to_reset_.signal();
        if (state_ == State::kResult) {
            setState(State::kLearning);
        }
    }

    void MatchEngine::setTarget(const Target target) {
        target_.store(target, std::memory_order::relaxed);
        to_update_target_.signal();
        to_reset_.signal();
    }

    bool MatchEngine::loadTargetFile(const juce::File& file) {
        juce::FileInputStream input(file);
        if (!input.openedOk()) {
            return false;
        }
        std::vector<float> freqs, dbs;
        while (!input.isExhausted()) {
            const auto line = input.readNextLine();
            const auto comma = line.indexOfChar(',');
            if (comma <= 0) {
                break;
            }
            freqs.push_back(line.substring(0, comma).trim().getFloatValue());
            dbs.push_back(line.substring(comma + 1).trim().getFloatValue());
        }
        if (freqs.size() < 2) {
            return false;
        }
        {
            std::lock_guard lock{file_mutex_};
            file_freqs_ = std::move(freqs);
            file_dbs_ = std::move(dbs);
        }
        setTarget(Target::kFile);
        return true;
    }

    bool MatchEngine::saveTargetFile(const juce::File& file) const {
        // saves the current reference curve in the upstream CSV format
        const auto& frame = frames_.getReader();
        if (!frame.valid) {
            return false;
        }
        juce::FileOutputStream output(file);
        if (!output.openedOk()) {
            return false;
        }
        output.setPosition(0);
        output.truncate();
        for (size_t i = 0; i < kNumPoints; ++i) {
            output << juce::String(frame.freqs[i]) << "," << juce::String(frame.target[i]) << "\n";
        }
        return true;
    }

    void MatchEngine::fit() {
        if (state_ != State::kLearning && state_ != State::kResult) {
            return;
        }
        frames_.pull();
        const auto& frame = frames_.getReader();
        if (!frame.valid) {
            return;
        }
        fit_freqs_.assign(frame.freqs.begin(), frame.freqs.end());
        fit_correction_.assign(frame.correction.begin(), frame.correction.end());
        fit_sample_rate_ = bridge_.getSampleRate();
        setState(State::kFitting);
        stopThread(4000);
        startThread(juce::Thread::Priority::normal);
    }

    void MatchEngine::run() {
        // the same greedy procedure as EqMatchOptimizer::fit, one band at a time,
        // so every band can be shown (and heard) as soon as it is found
        zlchore::eq_match::EqMatchOptimizer optimizer{fit_sample_rate_, fit_freqs_, fit_correction_};
        std::vector types{zldsp::filter::kPeak, zldsp::filter::kLowShelf, zldsp::filter::kHighShelf};
        std::vector<size_t> orders{2, 4, 6};
        std::vector<zldsp::filter::FilterParameters> paras;
        const auto should_return = [this] { return threadShouldExit(); };
        size_t suggested = 0;
        double previous_mse = 1e6;
        auto publish = [this](std::vector<zldsp::filter::FilterParameters> found, const size_t suggest, const bool done) {
            juce::MessageManager::callAsync([this, alive = std::weak_ptr<int>(alive_token_), results = std::move(found),
                                                suggest, done] {
                if (alive.expired() || state_ != State::kFitting) {
                    return;
                }
                fitted_ = results;
                suggested_ = std::min(suggest, fitted_.size());
                if (done) {
                    setState(State::kResult);
                    applyBands(suggested_);
                } else {
                    applyBands(fitted_.size());
                }
            });
        };
        const auto has_difference = std::ranges::any_of(fit_correction_, [](const float c) { return std::abs(c) > .05f; });
        for (size_t band = 0; has_difference && band < kBandNum; ++band) {
            const auto result = optimizer.addOneFilter(types, orders, should_return);
            if (threadShouldExit()) {
                return;
            }
            if (!result.has_value()) {
                break;
            }
            const auto [para, mse] = *result;
            paras.push_back(para);
            const auto delta = previous_mse - mse;
            previous_mse = mse;
            if (delta < 1.0 && suggested == 0) {
                suggested = band + 1;
            }
            if (delta < 4.0) {
                orders = {2};
            }
            if (delta < 1e-3) {
                break;
            }
            publish(paras, suggested > 0 ? suggested : paras.size(), false);
        }
        publish(paras, suggested > 0 ? suggested : paras.size(), true);
    }

    void MatchEngine::openSession() {
        if (session_open_ && bridge_.isEditing()) {
            return;
        }
        std::vector<juce::RangedAudioParameter*> paras;
        for (size_t b = 0; b < kBandNum; ++b) {
            const auto& bp = bridge_.params(b);
            for (const auto f : {
                     BandParams::kStatus, BandParams::kType, BandParams::kOrder, BandParams::kFreq,
                     BandParams::kGain, BandParams::kQ, BandParams::kLR, BandParams::kDynamicOn
                 }) {
                paras.push_back(bp[f]);
            }
        }
        bridge_.beginEdit(paras, "EQ Match");
        session_open_ = true;
    }

    void MatchEngine::applyBands(size_t n) {
        n = std::min(n, fitted_.size());
        openSession();
        // output the dry signal while bands are rewritten, like upstream, to avoid zipper noise
        bridge_.getController().setMatchBypassON(true);
        for (size_t b = 0; b < kBandNum; ++b) {
            const auto& bp = bridge_.params(b);
            if (b < n) {
                const auto& p = fitted_[b];
                bridge_.setPlain(bp[BandParams::kType], static_cast<float>(p.filter_type));
                bridge_.setPlain(bp[BandParams::kOrder], static_cast<float>(zlp::POrder::convertToIdx(p.order)));
                bridge_.setPlain(bp[BandParams::kFreq], static_cast<float>(p.freq));
                bridge_.setPlain(bp[BandParams::kGain], static_cast<float>(p.gain));
                bridge_.setPlain(bp[BandParams::kQ], static_cast<float>(p.q));
                bridge_.setPlain(bp[BandParams::kLR], static_cast<float>(zlp::kStereo));
                bridge_.setPlain(bp[BandParams::kDynamicOn], 0.f);
                bridge_.setPlain(bp[BandParams::kStatus], static_cast<float>(zlp::kOn));
            } else {
                bridge_.setPlain(bp[BandParams::kStatus], static_cast<float>(zlp::kOff));
            }
        }
        num_applied_ = n;
        startTimer(1000);
        if (on_state_changed) {
            on_state_changed();
        }
    }

    void MatchEngine::timerCallback() {
        stopTimer();
        bridge_.getController().setMatchBypassON(false);
    }

    void MatchEngine::finish() {
        stopThread(4000);
        if (session_open_ && bridge_.isEditing()) {
            bridge_.endEdit();
        }
        session_open_ = false;
        active_.store(false, std::memory_order::release);
        setState(State::kOff);
    }

    void MatchEngine::cancel() {
        stopThread(4000);
        if (session_open_ && bridge_.isEditing()) {
            bridge_.cancelEdit();
        }
        session_open_ = false;
        stopTimer();
        bridge_.getController().setMatchBypassON(false);
        active_.store(false, std::memory_order::release);
        setState(State::kOff);
    }

    void MatchEngine::drawSegment(size_t from_idx, float from_db, size_t to_idx, float to_db) {
        from_idx = std::min(from_idx, kNumPoints - 1);
        to_idx = std::min(to_idx, kNumPoints - 1);
        if (from_idx > to_idx) {
            std::swap(from_idx, to_idx);
            std::swap(from_db, to_db);
        }
        for (size_t i = from_idx; i <= to_idx; ++i) {
            const auto t = to_idx == from_idx ? 1.f : static_cast<float>(i - from_idx) / static_cast<float>(to_idx - from_idx);
            drawing_[i].store(from_db + t * (to_db - from_db), std::memory_order::relaxed);
        }
    }

    void MatchEngine::eraseSegment(size_t from_idx, size_t to_idx) {
        if (from_idx > to_idx) {
            std::swap(from_idx, to_idx);
        }
        for (size_t i = from_idx; i <= std::min(to_idx, kNumPoints - 1); ++i) {
            drawing_[i].store(kNotDrawn, std::memory_order::relaxed);
        }
    }

    void MatchEngine::clearDrawing() {
        for (auto& d : drawing_) {
            d.store(kNotDrawn, std::memory_order::relaxed);
        }
    }

    bool MatchEngine::hasDrawing() const {
        return std::ranges::any_of(drawing_, [](const auto& d) { return d.load(std::memory_order::relaxed) > -100.f; });
    }
}
