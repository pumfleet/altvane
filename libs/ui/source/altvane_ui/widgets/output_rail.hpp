// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

#include "level_meter.hpp"

namespace altvane::ui {
    /**
     * an editor's right rail: the output meter under an "OUT" caption; a click on the meter resets its peak hold
     * a mono main output shows its one channel in both bars
     */
    class OutputRail final : public juce::Component {
    public:
        /**
         * @param take_peaks the output peaks since the last call, in dB (e.g. a PeakExchange's take)
         */
        OutputRail(const juce::AudioProcessor& processor, std::function<std::array<float, 2>()> take_peaks);

        void tick(double dt);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseUp(const juce::MouseEvent& e) override;

    private:
        const juce::AudioProcessor& processor_;
        std::function<std::array<float, 2>()> take_peaks_;
        LevelMeter meter_;
        juce::Rectangle<float> label_area_, meter_area_, peak_area_;
    };
}
