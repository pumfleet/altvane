// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include "filter_icons.hpp"

#include <array>
#include <numbers>

#include "../../dsp/filter/ideal_filter/ideal.hpp"
#include "../../dsp/chore/decibels.hpp"

namespace altvane::eq::icons {
    const juce::Path& shape(const zldsp::filter::FilterType type) {
        static const auto paths = [] {
            std::array<juce::Path, 11> result;
            constexpr size_t kNum = 48;
            constexpr double kSampleRate = 96000.0;
            for (size_t t = 0; t < result.size(); ++t) {
                const auto ft = static_cast<zldsp::filter::FilterType>(t);
                zldsp::filter::Ideal<float, 16> ideal;
                ideal.prepare(kSampleRate);
                double gain = 0.0, q = 0.707, freq = 1000.0;
                size_t order = 2;
                switch (ft) {
                case zldsp::filter::kPeak: gain = 12.0; q = 1.0; break;
                case zldsp::filter::kLowShelf: gain = 12.0; freq = 500.0; break;
                case zldsp::filter::kHighShelf: gain = 12.0; freq = 2000.0; break;
                case zldsp::filter::kTiltShelf: gain = 12.0; break;
                case zldsp::filter::kFlatTilt: gain = 12.0; break;
                case zldsp::filter::kFlatGain: gain = 9.0; break;
                case zldsp::filter::kLowPass: freq = 2500.0; order = 4; break;
                case zldsp::filter::kHighPass: freq = 400.0; order = 4; break;
                case zldsp::filter::kNotch: q = 2.0; break;
                case zldsp::filter::kBandPass: q = 1.0; break;
                case zldsp::filter::kAllPass: break;
                default: break;
                }
                ideal.forceUpdate({ft, order, freq, gain, q});
                std::array<float, kNum> ws{}, mags{};
                for (size_t i = 0; i < kNum; ++i) {
                    const auto f = 40.0 * std::pow(25000.0 / 40.0, static_cast<double>(i) / (kNum - 1));
                    ws[i] = static_cast<float>(2.0 * std::numbers::pi * f / kSampleRate);
                }
                ideal.updateMagnitudeSquare(ws, mags);
                auto& p = result[t];
                for (size_t i = 0; i < kNum; ++i) {
                    auto db = static_cast<float>(zldsp::chore::squareGainToDecibels(static_cast<double>(mags[i])));
                    if (ft == zldsp::filter::kAllPass) {
                        // all-pass is flat in magnitude; draw a phase-like swirl instead of a straight line
                        db = 9.f * std::sin(static_cast<float>(i) / (kNum - 1) * std::numbers::pi_v<float> * 2.f);
                    }
                    const auto x = static_cast<float>(i) / (kNum - 1);
                    const auto y = .5f - std::clamp(db, -18.f, 18.f) / 36.f;
                    if (i == 0) {
                        p.startNewSubPath(x, y);
                    } else {
                        p.lineTo(x, y);
                    }
                }
            }
            return result;
        }();
        return paths[std::min(static_cast<size_t>(type), paths.size() - 1)];
    }

    void drawShape(juce::Graphics& g, const zldsp::filter::FilterType type, const juce::Rectangle<float> area,
                   const juce::Colour colour, const float stroke) {
        auto p = shape(type);
        p.applyTransform(juce::AffineTransform::scale(area.getWidth(), area.getHeight())
            .translated(area.getX(), area.getY()));
        g.setColour(colour);
        g.strokePath(p, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
