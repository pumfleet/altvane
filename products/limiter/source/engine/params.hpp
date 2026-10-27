// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "../zlp/zlp_definitions.hpp"

/**
 * the parameters Altvane Limiter adds to ZL Limiter's (zlp_definitions.hpp): the output stage after the limiter
 * they use the upstream parameter templates, so they live in the same state tree and the same version hint
 */
namespace altvane::limiter::params {
    class PDitherBits : public zlp::ChoiceParameters<PDitherBits> {
    public:
        static constexpr auto kID = "altvane_dither_bits";
        static constexpr auto kName = "Dither";
        inline static const auto kChoices = juce::StringArray{"Off", "24 bit", "20 bit", "16 bit"};
        static constexpr std::array kBits{0, 24, 20, 16};
        static constexpr int kDefaultI = 0;
    };

    class PDitherShape : public zlp::ChoiceParameters<PDitherShape> {
    public:
        static constexpr auto kID = "altvane_dither_shape";
        static constexpr auto kName = "Noise Shaping";
        inline static const auto kChoices = juce::StringArray{"Flat", "Shaped"};
        static constexpr int kDefaultI = 1;
    };

    class PDCFilter : public zlp::BoolParameters<PDCFilter> {
    public:
        static constexpr auto kID = "altvane_dc_filter";
        static constexpr auto kName = "DC Filter";
        static constexpr auto kDefaultV = false;
    };

    /**
     * ZL Limiter's layout with Altvane's parameters after it
     */
    inline juce::AudioProcessorValueTreeState::ParameterLayout getParameterLayout() {
        auto layout = zlp::getParameterLayout();
        layout.add(PDitherBits::get(), PDitherShape::get(), PDCFilter::get());
        return layout;
    }
}
