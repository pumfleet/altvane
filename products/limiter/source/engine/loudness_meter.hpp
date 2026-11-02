// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <altvane_ui/util/tri_buffer.hpp>

#include "../dsp/limiter/true_peak/true_peak_estimator.hpp"
#include "../dsp/loudness/k_weighting_filter.hpp"

namespace altvane::limiter {
    /**
     * what the loudness meter publishes, ten times a second
     * levels are LUFS (dBTP for the true peak); kNoLevel means nothing measurable yet
     */
    struct LoudnessReadings {
        static constexpr float kNoLevel = -120.f;
        // the short-term loudness distribution, 1 LU per bin from kHistogramLow LUFS
        static constexpr int kHistogramLow = -60;
        static constexpr size_t kHistogramBins = 60;

        float momentary{kNoLevel}, short_term{kNoLevel}, integrated{kNoLevel};
        float max_momentary{kNoLevel}, max_short_term{kNoLevel};
        // loudness range (EBU Tech 3342), in LU
        float range{0.f};
        // the highest true peak since the last reset, over all channels
        float true_peak_max{kNoLevel};
        bool has_integrated{false}, has_range{false};
        // seconds of audio in the integrated measurement
        double seconds{0.0};
        // counts up on every reset, so a display can clear its history
        uint32_t generation{0};
        std::array<float, kHistogramBins> histogram{};
    };

    /**
     * an ITU-R BS.1770-4 / EBU R128 loudness meter that runs on the audio thread, so the measurement goes on
     * while the editor is closed and in offline renders
     *
     * momentary (400 ms) and short-term (3 s) loudness come from 10 ms slices of K-weighted energy. They are
     * published every 100 ms, but their maxima follow every slice, so a burst that does not line up with the 100 ms
     * updates is not under-read (EBU Tech 3341 cases 10 to 14). Integrated loudness and loudness range are gated
     * from the 100 ms blocks, through histograms of the block loudness with 0.01 LU bins, which are allocated in
     * prepare(), so process() never allocates
     */
    class LoudnessMeter {
    public:
        void prepare(const double sample_rate, const size_t num_channels, const size_t maximum_block_size) {
            num_channels_ = std::clamp<size_t>(num_channels, 1, kMaxChannels);
            slice_samples_ = std::max<size_t>(1, static_cast<size_t>(std::lround(sample_rate * .01)));
            k_weighting_.prepare(sample_rate, num_channels_);
            true_peak_.prepare(num_channels_, maximum_block_size);
            for (size_t c = 0; c < kMaxChannels; ++c) {
                weighted_[c].assign(maximum_block_size, 0.0);
                weighted_pointers_[c] = weighted_[c].data();
                peaks_[c].assign(maximum_block_size, 0.f);
            }
            gating_.assign(kGatingBins, Bin{});
            range_.assign(kGatingBins, Bin{});
            clear();
        }

        /**
         * ask for a reset; the audio thread does it at the start of its next block (any thread)
         */
        void requestReset() { reset_requested_.store(true, std::memory_order::release); }

        /**
         * whether blocks count towards the integrated loudness, range and maxima; momentary and short-term
         * loudness always follow the signal (audio thread)
         */
        void setAccumulating(const bool accumulating) {
            accumulating_ = accumulating;
            // the slice in progress has some uncounted audio in it
            slice_paused_ = slice_paused_ || !accumulating;
        }

        void process(std::span<const float* const> channels, const size_t num_samples) {
            if (reset_requested_.exchange(false, std::memory_order::acq_rel)) {
                clear();
            }
            const auto num_channels = std::min(channels.size(), num_channels_);
            const auto capacity = weighted_[0].size();
            if (num_channels == 0 || capacity == 0) {
                return;
            }
            // hosts may send more than the prepared block size: measure it in pieces
            for (size_t start = 0; start < num_samples; start += capacity) {
                std::array<const float*, kMaxChannels> piece{};
                for (size_t c = 0; c < num_channels; ++c) {
                    piece[c] = channels[c] + start;
                }
                processPiece(std::span{piece.data(), num_channels}, std::min(capacity, num_samples - start));
            }
        }

        /**
         * the latest readings (one consumer thread)
         */
        const LoudnessReadings& pull() {
            readings_.pull();
            return readings_.getReader();
        }

        static float energyToLoudness(const double energy) {
            return energy > 0.0 ? static_cast<float>(-0.691 + 10.0 * std::log10(energy)) : LoudnessReadings::kNoLevel;
        }

    private:
        void processPiece(const std::span<const float*> channels, const size_t num_samples) {
            const auto num_channels = channels.size();

            for (size_t c = 0; c < num_channels; ++c) {
                std::copy(channels[c], channels[c] + num_samples, weighted_[c].begin());
                if (accumulating_) {
                    true_peak_.processBlock(c, channels[c], peaks_[c].data(), num_samples);
                    const auto peak = *std::max_element(peaks_[c].begin(), peaks_[c].begin() +
                                                        static_cast<std::ptrdiff_t>(num_samples));
                    true_peak_max_ = std::max(true_peak_max_, peak);
                } else {
                    true_peak_.updateHistoryOnly(c, channels[c], num_samples);
                }
            }
            k_weighting_.process(std::span<double*>{weighted_pointers_.data(), num_channels}, num_samples);

            size_t i = 0;
            while (i < num_samples) {
                const auto count = std::min(num_samples - i, slice_samples_ - slice_filled_);
                for (size_t c = 0; c < num_channels; ++c) {
                    const auto* x = weighted_[c].data() + i;
                    double sum = 0.0;
                    for (size_t n = 0; n < count; ++n) {
                        sum += x[n] * x[n];
                    }
                    slice_energy_ += sum;
                }
                slice_filled_ += count;
                i += count;
                if (slice_filled_ == slice_samples_) {
                    finishSlice();
                }
            }
        }

        static constexpr size_t kMaxChannels = 2;
        // 10 ms slices: 10 make a 100 ms step (the gating blocks' hop and the publishing rate), 40 the momentary
        // window and 300 the short-term window
        static constexpr size_t kSlicesPerStep = 10, kMomentarySlices = 40, kShortTermSlices = 300;
        // gating histograms: -70 .. +10 LUFS in 0.01 LU bins
        static constexpr double kHistogramFloor = -70.0, kBinsPerLU = 100.0;
        static constexpr size_t kGatingBins = 8000;

        struct Bin {
            double energy{0.0};
            uint64_t count{0};
        };

        size_t num_channels_{2}, slice_samples_{480};
        zldsp::loudness::KWeightingFilter<double> k_weighting_{false};
        zldsp::limiter::TruePeakEstimator<float> true_peak_;
        std::array<std::vector<double>, kMaxChannels> weighted_;
        std::array<double*, kMaxChannels> weighted_pointers_{};
        std::array<std::vector<float>, kMaxChannels> peaks_;

        double slice_energy_{0.0};
        size_t slice_filled_{0};
        // the mean energy of the last 300 slices, newest at slices_head_
        std::array<double, kShortTermSlices> slices_{};
        size_t slices_head_{0}, slices_count_{0}, slice_in_step_{0};

        std::vector<Bin> gating_, range_;
        Bin gating_total_, range_total_;
        std::array<uint64_t, LoudnessReadings::kHistogramBins> histogram_{};
        float max_momentary_{LoudnessReadings::kNoLevel}, max_short_term_{LoudnessReadings::kNoLevel};
        float true_peak_max_{0.f};
        uint64_t slices_measured_{0};
        uint32_t generation_{0};
        bool accumulating_{true};
        // the slice in progress saw audio while not counting; the number of counted slices in a row
        bool slice_paused_{false};
        size_t counted_run_{0};
        std::atomic<bool> reset_requested_{false};

        ui::TriBuffer<LoudnessReadings> readings_;

        void clear() {
            k_weighting_.reset();
            true_peak_.reset();
            slice_energy_ = 0.0;
            slice_filled_ = 0;
            slices_.fill(0.0);
            slices_head_ = 0;
            slices_count_ = 0;
            slice_in_step_ = 0;
            std::fill(gating_.begin(), gating_.end(), Bin{});
            std::fill(range_.begin(), range_.end(), Bin{});
            gating_total_ = {};
            range_total_ = {};
            histogram_.fill(0);
            max_momentary_ = LoudnessReadings::kNoLevel;
            max_short_term_ = LoudnessReadings::kNoLevel;
            true_peak_max_ = 0.f;
            slices_measured_ = 0;
            slice_paused_ = !accumulating_;
            counted_run_ = 0;
            ++generation_;
            publish(LoudnessReadings::kNoLevel, LoudnessReadings::kNoLevel);
        }

        double meanOfLastSlices(const size_t n) const {
            double sum = 0.0;
            for (size_t k = 0; k < n; ++k) {
                sum += slices_[(slices_head_ + kShortTermSlices - k) % kShortTermSlices];
            }
            return sum / static_cast<double>(n);
        }

        static size_t binOf(const float loudness) {
            const auto index = std::floor((static_cast<double>(loudness) - kHistogramFloor) * kBinsPerLU);
            return static_cast<size_t>(std::clamp(index, 0.0, static_cast<double>(kGatingBins - 1)));
        }

        static double binLoudness(const size_t bin) {
            return kHistogramFloor + (static_cast<double>(bin) + .5) / kBinsPerLU;
        }

        static void add(std::vector<Bin>& bins, Bin& total, const float loudness, const double energy) {
            auto& bin = bins[binOf(loudness)];
            bin.energy += energy;
            ++bin.count;
            total.energy += energy;
            ++total.count;
        }

        void finishSlice() {
            slices_head_ = (slices_head_ + 1) % kShortTermSlices;
            slices_[slices_head_] = slice_energy_ / static_cast<double>(slice_samples_);
            slices_count_ = std::min(slices_count_ + 1, kShortTermSlices);
            slice_energy_ = 0.0;
            slice_filled_ = 0;

            // a window only counts when every slice in it was measured while counting, so audio heard while the
            // transport was stopped never reaches the integrated loudness, the range or the maxima on resume
            if (accumulating_ && !slice_paused_) {
                ++slices_measured_;
                counted_run_ = std::min(counted_run_ + 1, kShortTermSlices);
            } else {
                counted_run_ = 0;
            }
            slice_paused_ = !accumulating_;

            auto momentary = LoudnessReadings::kNoLevel, short_term = LoudnessReadings::kNoLevel;
            double momentary_energy = 0.0, short_term_energy = 0.0;
            if (slices_count_ >= kMomentarySlices) {
                momentary_energy = meanOfLastSlices(kMomentarySlices);
                momentary = energyToLoudness(momentary_energy);
                if (counted_run_ >= kMomentarySlices) {
                    max_momentary_ = std::max(max_momentary_, momentary);
                }
            }
            if (slices_count_ >= kShortTermSlices) {
                short_term_energy = meanOfLastSlices(kShortTermSlices);
                short_term = energyToLoudness(short_term_energy);
                if (counted_run_ >= kShortTermSlices) {
                    max_short_term_ = std::max(max_short_term_, short_term);
                }
            }

            if (++slice_in_step_ < kSlicesPerStep) {
                return;
            }
            slice_in_step_ = 0;
            // a 400 ms gating block every 100 ms (75 % overlap); the absolute gate is -70 LUFS
            if (counted_run_ >= kMomentarySlices && momentary > kHistogramFloor) {
                add(gating_, gating_total_, momentary, momentary_energy);
            }
            if (counted_run_ >= kShortTermSlices && short_term > kHistogramFloor) {
                add(range_, range_total_, short_term, short_term_energy);
                const auto bin = static_cast<int>(std::floor(short_term)) - LoudnessReadings::kHistogramLow;
                if (bin >= 0 && bin < static_cast<int>(LoudnessReadings::kHistogramBins)) {
                    ++histogram_[static_cast<size_t>(bin)];
                }
            }
            publish(momentary, short_term);
        }

        /**
         * the mean energy of the blocks at or above a relative gate below the mean of all blocks
         */
        static Bin gated(const std::vector<Bin>& bins, const Bin& total, const double relative_lu) {
            if (total.count == 0) {
                return {};
            }
            const auto gate = energyToLoudness(total.energy / static_cast<double>(total.count)) + relative_lu;
            Bin result;
            for (auto b = binOf(static_cast<float>(gate)); b < bins.size(); ++b) {
                result.energy += bins[b].energy;
                result.count += bins[b].count;
            }
            return result;
        }

        static double percentile(const std::vector<Bin>& bins, const size_t first_bin, const uint64_t count,
                                 const double p) {
            const auto rank = static_cast<uint64_t>(std::llround(p * static_cast<double>(count - 1)));
            uint64_t seen = 0;
            for (auto b = first_bin; b < bins.size(); ++b) {
                seen += bins[b].count;
                if (seen > rank) {
                    return binLoudness(b);
                }
            }
            return binLoudness(bins.size() - 1);
        }

        void publish(const float momentary, const float short_term) {
            auto& r = readings_.getWriter();
            r.momentary = momentary;
            r.short_term = short_term;
            r.max_momentary = max_momentary_;
            r.max_short_term = max_short_term_;
            r.true_peak_max = true_peak_max_ > 0.f
                                  ? 20.f * std::log10(true_peak_max_)
                                  : LoudnessReadings::kNoLevel;
            r.seconds = static_cast<double>(slices_measured_) * .01;
            r.generation = generation_;

            // integrated: the relative gate is 10 LU below the mean of the blocks above the absolute gate
            const auto integrated = gated(gating_, gating_total_, -10.0);
            r.has_integrated = integrated.count > 0;
            r.integrated = r.has_integrated
                               ? energyToLoudness(integrated.energy / static_cast<double>(integrated.count))
                               : LoudnessReadings::kNoLevel;

            // range: 10th to 95th percentile of the short-term blocks within 20 LU of their mean
            r.has_range = false;
            r.range = 0.f;
            if (range_total_.count > 0) {
                const auto gate = energyToLoudness(range_total_.energy / static_cast<double>(range_total_.count)) - 20.0;
                const auto first = binOf(static_cast<float>(gate));
                uint64_t count = 0;
                for (auto b = first; b < range_.size(); ++b) {
                    count += range_[b].count;
                }
                if (count > 1) {
                    r.has_range = true;
                    r.range = static_cast<float>(percentile(range_, first, count, .95) -
                                                 percentile(range_, first, count, .10));
                }
            }

            for (size_t b = 0; b < LoudnessReadings::kHistogramBins; ++b) {
                r.histogram[b] = static_cast<float>(histogram_[b]);
            }
            readings_.publish();
        }
    };
}
