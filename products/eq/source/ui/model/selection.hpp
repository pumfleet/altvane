// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <bitset>
#include <cstdint>

#include "../common.hpp"
#include "engine_bridge.hpp"

namespace altvane::eq {
    /**
     * which bands are selected / hovered; message thread only
     */
    class Selection {
    public:
        static constexpr size_t kNone = kBandNum;

        size_t getPrimary() const { return primary_; }
        size_t getHovered() const { return hovered_; }
        bool isSelected(const size_t band) const { return band < kBandNum && selected_[band]; }
        size_t count() const { return selected_.count(); }
        bool isEmpty() const { return selected_.none(); }
        const std::bitset<kBandNum>& getBits() const { return selected_; }
        std::uint32_t getVersion() const { return version_; }

        void select(const size_t band) {
            if (band >= kBandNum) {
                clear();
                return;
            }
            if (selected_.count() == 1 && selected_[band] && primary_ == band) {
                return;
            }
            selected_.reset();
            selected_.set(band);
            primary_ = band;
            ++version_;
        }

        void add(const size_t band) {
            if (band >= kBandNum) {
                return;
            }
            selected_.set(band);
            primary_ = band;
            ++version_;
        }

        void toggle(const size_t band) {
            if (band >= kBandNum) {
                return;
            }
            selected_.flip(band);
            if (selected_[band]) {
                primary_ = band;
            } else if (primary_ == band) {
                primary_ = firstSelected();
            }
            ++version_;
        }

        void setBits(const std::bitset<kBandNum>& bits) {
            if (bits == selected_) {
                return;
            }
            selected_ = bits;
            if (primary_ >= kBandNum || !selected_[primary_]) {
                primary_ = firstSelected();
            }
            ++version_;
        }

        void clear() {
            if (selected_.none() && primary_ == kNone) {
                return;
            }
            selected_.reset();
            primary_ = kNone;
            ++version_;
        }

        void setHovered(const size_t band) {
            if (band != hovered_) {
                hovered_ = band;
                ++version_;
            }
        }

        /**
         * drop bands that no longer exist
         */
        void prune(const EngineBridge& bridge) {
            auto bits = selected_;
            for (size_t b = 0; b < kBandNum; ++b) {
                if (bits[b] && !bridge.band(b).isActive()) {
                    bits.reset(b);
                }
            }
            setBits(bits);
            if (hovered_ < kBandNum && !bridge.band(hovered_).isActive()) {
                setHovered(kNone);
            }
        }

    private:
        std::bitset<kBandNum> selected_;
        size_t primary_{kNone};
        size_t hovered_{kNone};
        std::uint32_t version_{0};

        size_t firstSelected() const {
            for (size_t b = 0; b < kBandNum; ++b) {
                if (selected_[b]) {
                    return b;
                }
            }
            return kNone;
        }
    };
}
