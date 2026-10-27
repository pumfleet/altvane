// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "engine.hpp"

#include <numeric>

namespace altvane::leveler::dsp {
    namespace {
        float dbToGain(const float db) {
            return std::pow(10.f, db * .05f);
        }

        double mean(const auto& values) {
            return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        }
    }

    void Engine::prepare(const double sample_rate, const int max_block) {
        sample_rate_ = sample_rate;
        max_block_ = std::max(max_block, 1);
        hop_ = std::max(1, static_cast<int>(std::lround(sample_rate * kHopSeconds)));
        hop_shared_.store(hop_, std::memory_order::relaxed);
        for (size_t c = 0; c < kMaxChannels; ++c) {
            k_filters_[c].prepare(sample_rate);
            key_filters_[c].prepare(sample_rate);
            dry_[c].assign(static_cast<size_t>(max_block_), 0.f);
        }
        for (auto& v : voicing_) {
            v.prepare(sample_rate, hop_);
        }
        rider_.prepare(static_cast<double>(hop_) / sample_rate);
        size_t size = 1;
        while (size < static_cast<size_t>(hop_ * kLookaheadHops + 1)) {
            size <<= 1;
        }
        for (auto& d : delay_) {
            d.assign(size, 0.f);
        }
        delay_mask_ = size - 1;
        fade_len_ = 2 * hop_;
        reset();
    }

    void Engine::reset() {
        for (size_t c = 0; c < kMaxChannels; ++c) {
            k_filters_[c].reset();
            key_filters_[c].reset();
            std::fill(delay_[c].begin(), delay_[c].end(), 0.f);
        }
        for (auto& v : voicing_) {
            v.reset();
        }
        hop_pos_ = 0;
        hop_energy_ = key_energy_ = 0.0;
        energies_.fill(0.0);
        voiced_.fill(false);
        key_energies_.fill(0.0);
        level_slot_ = key_slot_ = 0;
        key_memory_db_ = kSilenceDb;
        key_fill_hops_ = 0;
        records_.fill({});
        record_slot_ = 0;
        delay_write_ = 0;
        delay_hops_ = old_delay_hops_ = controls_.lookahead.load(std::memory_order::relaxed) ? kLookaheadHops : 0;
        fade_pos_ = fade_len_;

        const auto lo = std::min(controls_.range_min.load(), controls_.range_max.load());
        const auto hi = std::max(controls_.range_min.load(), controls_.range_max.load());
        const auto start = static_cast<Mode>(controls_.mode.load()) == Mode::kRead
                               ? controls_.ride.load()
                               : std::clamp(controls_.idle.load(), lo, hi);
        rider_.reset(start);
        gain_ = dbToGain(start);
        gain_step_ = 0.f;
        gain_db_.store(start);
    }

    int Engine::getLatency() const {
        return controls_.lookahead.load(std::memory_order::relaxed) ? kLookaheadHops * getHopSamples() : 0;
    }

    void Engine::setPublishing(const bool publishing) {
        publishing_.store(publishing, std::memory_order::relaxed);
    }

    float Engine::delayed(const size_t channel) const {
        const auto& d = delay_[channel];
        const auto now = d[(delay_write_ - static_cast<size_t>(delay_hops_ * hop_)) & delay_mask_];
        if (fade_pos_ >= fade_len_) {
            return now;
        }
        const auto before = d[(delay_write_ - static_cast<size_t>(old_delay_hops_ * hop_)) & delay_mask_];
        const auto a = static_cast<float>(fade_pos_) / static_cast<float>(fade_len_);
        return before + (now - before) * a;
    }

    void Engine::process(float* const* main, const int channels, const float* const* key, const int key_channels,
                         const int n) {
        const auto chans = static_cast<size_t>(std::clamp(channels, 1, static_cast<int>(kMaxChannels)));
        channels_ = chans;
        const auto key_chans = key == nullptr ? size_t{0} : static_cast<size_t>(std::clamp(key_channels, 0, 2));
        for (int i = 0; i < n; ++i) {
            const auto idx = static_cast<size_t>(i);
            for (size_t c = 0; c < chans; ++c) {
                const auto x = main[c][idx];
                const auto k = k_filters_[c].process(x);
                hop_energy_ += k * k;
                voicing_[c].push(x);
                delay_[c][delay_write_ & delay_mask_] = x;
            }
            for (size_t c = 0; c < key_chans; ++c) {
                const auto k = key_filters_[c].process(key[c][idx]);
                key_energy_ += k * k;
            }
            for (size_t c = 0; c < chans; ++c) {
                const auto y = delayed(c);
                dry_[c][idx] = y;
                main[c][idx] = y * gain_;
            }
            ++delay_write_;
            if (fade_pos_ < fade_len_) {
                ++fade_pos_;
            }
            gain_ += gain_step_;
            if (++hop_pos_ >= hop_) {
                hop_pos_ = 0;
                endHop();
            }
        }
    }

    void Engine::endHop() {
        const auto& c = controls_;
        const auto hop = static_cast<double>(hop_);

        // levels: 20 ms for the floor, 100 ms for the display, 100 ms of voiced hops only for riding
        energies_[level_slot_] = hop_energy_ / hop;
        // the most voice-like channel decides: a voice panned or recorded on one side still counts
        auto v = voicing_[0].analyse();
        for (size_t ch = 1; ch < channels_; ++ch) {
            if (const auto other = voicing_[ch].analyse(); other.periodicity > v.periodicity) {
                v = other;
            }
        }
        double fast = 0.0;
        for (size_t i = 0; i < kFastHops; ++i) {
            fast += energies_[(level_slot_ + kLevelHops - i) % kLevelHops];
        }
        const auto fast_db = loudnessDb(fast / static_cast<double>(kFastHops));
        const auto in_db = loudnessDb(mean(energies_));

        const auto target = c.target.load(std::memory_order::relaxed);
        const auto floor = target - 24.f - c.sensitivity.load(std::memory_order::relaxed);
        HopState state;
        if (fast_db < floor) {
            state = HopState::kSilent;
        } else if (v.periodicity >= .6f && v.high_ratio < .6f) {
            state = HopState::kVoiced;
        } else {
            state = HopState::kUnvoiced;
        }
        voiced_[level_slot_] = state == HopState::kVoiced;
        double voiced_energy = 0.0;
        int voiced_hops = 0;
        for (size_t i = 0; i < kLevelHops; ++i) {
            if (voiced_[i]) {
                voiced_energy += energies_[i];
                ++voiced_hops;
            }
        }
        const auto voiced_db = voiced_hops > 0 ? loudnessDb(voiced_energy / voiced_hops) : kSilenceDb;
        if (state == HopState::kVoiced && learning_.load(std::memory_order::relaxed)) {
            learn_energy_.store(learn_energy_.load(std::memory_order::relaxed) + energies_[level_slot_],
                                std::memory_order::relaxed);
            learn_hops_.fetch_add(1, std::memory_order::relaxed);
        }
        level_slot_ = (level_slot_ + 1) % kLevelHops;
        hop_energy_ = 0.0;

        // the music key: the target follows its level relative to its own long-term level
        key_energies_[key_slot_] = key_energy_ / hop;
        key_slot_ = (key_slot_ + 1) % kKeyHops;
        key_energy_ = 0.0;
        const auto key_db = loudnessDb(mean(key_energies_));
        auto offset = 0.f;
        if (key_db > -60.f) {
            // until the window has filled with music, the memory starts from scratch
            if (key_fill_hops_ < kKeyHops) {
                ++key_fill_hops_;
                key_memory_db_ = key_db;
            }
            const auto a = static_cast<float>(1.0 - std::exp(-hop / sample_rate_ / kKeyMemorySeconds));
            key_memory_db_ += (key_db - key_memory_db_) * a;
            offset = c.music.load(std::memory_order::relaxed) * std::clamp(key_db - key_memory_db_, -12.f, 12.f);
        }

        Rider::Settings settings;
        settings.target_db = target + offset;
        settings.speed = c.speed.load(std::memory_order::relaxed);
        settings.guard = c.guard.load(std::memory_order::relaxed);
        settings.range_min = c.range_min.load(std::memory_order::relaxed);
        settings.range_max = c.range_max.load(std::memory_order::relaxed);
        settings.idle_db = c.idle.load(std::memory_order::relaxed);

        float gain_db;
        if (c.manual.load(std::memory_order::relaxed) || controls_.manual_pending.exchange(false)) {
            gain_db = c.manual_gain.load(std::memory_order::relaxed);
            rider_.setGain(gain_db);
        } else if (static_cast<Mode>(c.mode.load(std::memory_order::relaxed)) == Mode::kRead) {
            gain_db = c.ride.load(std::memory_order::relaxed);
            rider_.setGain(gain_db);
        } else {
            gain_db = rider_.update(state, voiced_db, fast_db, settings);
        }
        gain_step_ = (dbToGain(gain_db) - gain_) / static_cast<float>(hop_);
        gain_db_.store(gain_db, std::memory_order::relaxed);
        state_.store(static_cast<int>(state), std::memory_order::relaxed);

        // the lookahead may change between hops: crossfade to the new delay
        const auto delay_hops = c.lookahead.load(std::memory_order::relaxed) ? kLookaheadHops : 0;
        if (delay_hops != delay_hops_) {
            old_delay_hops_ = delay_hops_;
            delay_hops_ = delay_hops;
            fade_pos_ = 0;
        }

        // history, lined up with the delayed audio the gain is applied to
        records_[record_slot_] = {in_db, key_db, settings.target_db, floor, state};
        const auto& r = records_[(record_slot_ + kRecordHops - static_cast<size_t>(delay_hops_)) % kRecordHops];
        record_slot_ = (record_slot_ + 1) % kRecordHops;
        if (publishing_.load(std::memory_order::relaxed)) {
            Column column;
            column.in_db = r.in_db;
            // what is heard: in bypass, the delayed input itself
            column.out_db = r.in_db <= kSilenceDb || c.bypass.load(std::memory_order::relaxed)
                                ? r.in_db
                                : r.in_db + gain_db + c.output.load(std::memory_order::relaxed);
            column.key_db = r.key_db;
            column.gain_db = gain_db;
            column.target_db = r.target_db;
            column.floor_db = r.floor_db;
            column.state = r.state;
            columns_.push(column);
        }
    }

    void Engine::startLearn() {
        learn_energy_.store(0.0);
        learn_hops_.store(0);
        learning_.store(true);
    }

    std::optional<float> Engine::stopLearn() {
        learning_.store(false);
        const auto hops = learn_hops_.load();
        if (hops < 20) {
            return std::nullopt;
        }
        return loudnessDb(learn_energy_.load() / hops);
    }
}
