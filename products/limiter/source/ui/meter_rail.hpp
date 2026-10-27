// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <altvane_ui/util/spring.hpp>

#include "common.hpp"
#include "model/level_feed.hpp"

namespace altvane::limiter {
    /**
     * the right rail: output peaks (true peaks when the limiter works in true peak), gain reduction and
     * momentary / short-term loudness with the integrated value and the target marked
     * the readouts on top hold the highest value until clicked
     */
    class MeterRail final : public juce::Component {
    public:
        explicit MeterRail(PluginProcessor& p);

        void tick(double dt, const LevelFeed& feed, const LoudnessReadings& loudness);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseUp(const juce::MouseEvent& e) override;

        void resetHolds();

    private:
        PluginProcessor& p_ref_;
        std::array<float, 2> out_{LevelFeed::kFloor, LevelFeed::kFloor};
        float out_hold_{LevelFeed::kFloor}, reduction_{0.f}, reduction_hold_{0.f};
        ui::Spring momentary_{-60.f, 8.f}, short_term_{-60.f, 6.f};
        float integrated_{LoudnessReadings::kNoLevel};
        bool has_integrated_{false};
        float ceiling_{-.1f};
        std::optional<float> target_;
        int scale_{0};
        bool true_peak_{true};

        juce::Rectangle<float> readouts_, out_area_, out_scale_, gr_area_, gr_scale_, lufs_area_, lufs_scale_, labels_;

        // the loudness scale: reference level and the displayed range, LUFS
        float loudnessReference() const;
        std::pair<float, float> loudnessRange() const;
    };
}
