// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../model/comp_bridge.hpp"

namespace altvane::compressor {
    /**
     * the static transfer curve, computed with the engine's own gain computers
     * and the engine's rules for direction, range, wet and makeup (see CompressController::appleSideBuffer)
     */
    class TransferModel final {
    public:
        struct Settings {
            zlp::PCompDirection::Direction direction{zlp::PCompDirection::kCompress};
            float threshold{-32.f}, ratio{3.f}, knee{8.f}, curve{50.f}, floor{-140.f};
            float range{80.f}, wet{100.f}, makeup{0.f};
            bool range_inf{false};

            bool operator==(const Settings& o) const {
                return direction == o.direction && juce::exactlyEqual(threshold, o.threshold)
                       && juce::exactlyEqual(ratio, o.ratio) && juce::exactlyEqual(knee, o.knee)
                       && juce::exactlyEqual(curve, o.curve) && juce::exactlyEqual(floor, o.floor)
                       && juce::exactlyEqual(range, o.range) && juce::exactlyEqual(wet, o.wet)
                       && juce::exactlyEqual(makeup, o.makeup) && range_inf == o.range_inf;
            }
        };

        /**
         * read the current parameters; returns true when the curve changed
         */
        bool sync(const CompBridge& bridge);

        void setSettings(const Settings& s);

        const Settings& getSettings() const { return s_; }

        /**
         * the gain change (dB) at a steady input level, without makeup
         */
        float gainFor(float input_db);

        /**
         * the output level (dB) at a steady input level
         */
        float outputFor(float input_db);

    private:
        Settings s_;
        bool valid_{false};
        zldsp::compressor::CompressionComputer<float, true> compression_;
        zldsp::compressor::ExpansionComputer<float, true> expansion_;
        zldsp::compressor::InflationComputer<float, true> inflation_;
    };
}
