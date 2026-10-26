// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "band_ops.hpp"

namespace altvane::eq::band_ops {
    using zldsp::filter::FilterType;

    namespace {
        float defaultPlain(const juce::RangedAudioParameter* para) {
            return para->convertFrom0to1(para->getDefaultValue());
        }

        bool isShelfLike(const FilterType t) {
            return t == FilterType::kLowShelf || t == FilterType::kHighShelf
                || t == FilterType::kTiltShelf || t == FilterType::kFlatTilt;
        }
    }

    FilterType inferType(const double freq, const bool shelf_modifier) {
        if (freq < 30.0) {
            return shelf_modifier ? FilterType::kLowShelf : FilterType::kHighPass;
        }
        if (freq > 15000.0) {
            return shelf_modifier ? FilterType::kHighShelf : FilterType::kLowPass;
        }
        if (shelf_modifier) {
            return freq < 1000.0 ? FilterType::kLowShelf : FilterType::kHighShelf;
        }
        return FilterType::kPeak;
    }

    size_t add(EngineBridge& bridge, const double freq, const float gain, const FilterType type) {
        const auto band = bridge.findOffBand();
        if (band >= kBandNum) {
            return band;
        }
        const auto& bp = bridge.params(band);
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        values.reserve(BandParams::kNumFields);
        for (size_t f = 0; f < BandParams::kNumFields; ++f) {
            values.emplace_back(bp.paras[f], defaultPlain(bp.paras[f]));
        }
        const auto is_cut = type == FilterType::kLowPass || type == FilterType::kHighPass;
        const auto has_gain = type == FilterType::kPeak || isShelfLike(type) || type == FilterType::kFlatGain;
        const auto g = has_gain ? gain : 0.f;
        auto set = [&](const BandParams::Field f, const float v) {
            values[static_cast<size_t>(f)].second = v;
        };
        set(BandParams::kType, static_cast<float>(type));
        set(BandParams::kOrder, is_cut ? 2.f : 1.f);
        set(BandParams::kFreq, static_cast<float>(freq));
        set(BandParams::kGain, g);
        set(BandParams::kTargetGain, g);
        set(BandParams::kSideFreq, static_cast<float>(freq));
        set(BandParams::kStatus, static_cast<float>(zlp::kOn));
        bridge.setNow(values, "Add Band");
        return band;
    }

    void remove(EngineBridge& bridge, const std::vector<size_t>& bands) {
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        for (const auto b : bands) {
            values.emplace_back(bridge.params(b)[BandParams::kStatus], static_cast<float>(zlp::kOff));
            // stop threshold learning on a removed band
            values.emplace_back(bridge.params(b)[BandParams::kDynamicLearn], 0.f);
        }
        bridge.setNow(values, bands.size() > 1 ? "Delete Bands" : "Delete Band");
    }

    void toggleBypass(EngineBridge& bridge, const std::vector<size_t>& bands) {
        if (bands.empty()) {
            return;
        }
        // all follow the first band, so a mixed selection becomes consistent
        const auto to_on = bridge.band(bands.front()).status == zlp::kBypass;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        for (const auto b : bands) {
            values.emplace_back(bridge.params(b)[BandParams::kStatus],
                                static_cast<float>(to_on ? zlp::kOn : zlp::kBypass));
        }
        bridge.setNow(values, to_on ? "Enable Band" : "Bypass Band");
    }

    void setDynamic(EngineBridge& bridge, const size_t band, const bool on) {
        const auto& b = bridge.band(band);
        const auto& bp = bridge.params(band);
        if (on) {
            // start the dynamic range a few dB beyond the static gain, so the effect is visible
            const auto target = b.hasGain()
                ? std::clamp(b.gain + (b.gain >= 0.f ? -6.f : 6.f), -30.f, 30.f)
                : b.gain;
            bridge.setNow({
                              {bp[BandParams::kDynamicOn], 1.f},
                              {bp[BandParams::kTargetGain], target},
                              {bp[BandParams::kSideFreq], b.freq},
                              {bp[BandParams::kSideQ], b.q},
                          }, "Enable Dynamics");
        } else {
            bridge.setNow({
                              {bp[BandParams::kDynamicOn], 0.f},
                              {bp[BandParams::kDynamicLearn], 0.f},
                          }, "Disable Dynamics");
        }
    }

    void setType(EngineBridge& bridge, const size_t band, const FilterType type) {
        const auto& b = bridge.band(band);
        const auto& bp = bridge.params(band);
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values{
            {bp[BandParams::kType], static_cast<float>(type)}
        };
        // cuts read better with a steeper default slope
        const auto was_cut = b.type == FilterType::kLowPass || b.type == FilterType::kHighPass;
        const auto is_cut = type == FilterType::kLowPass || type == FilterType::kHighPass;
        if (is_cut && !was_cut && b.order_idx < 2) {
            values.emplace_back(bp[BandParams::kOrder], 2.f);
        } else if (!is_cut && was_cut && b.order_idx > 1) {
            values.emplace_back(bp[BandParams::kOrder], 1.f);
        }
        bridge.setNow(values, "Change Shape");
    }

    void setOrder(EngineBridge& bridge, const size_t band, const size_t order_idx) {
        bridge.setNow({{bridge.params(band)[BandParams::kOrder], static_cast<float>(order_idx)}}, "Change Slope");
    }

    void setStereo(EngineBridge& bridge, const size_t band, const zlp::FilterStereo lr) {
        bridge.setNow({{bridge.params(band)[BandParams::kLR], static_cast<float>(lr)}}, "Change Stereo Placement");
    }

    float getNodeDB(const BandSnapshot& b, const bool target) {
        const auto g = target ? b.target_gain : b.gain;
        if (b.type == FilterType::kPeak || b.type == FilterType::kFlatGain) {
            return g;
        }
        return isShelfLike(b.type) ? .5f * g : 0.f;
    }

    float nodeDBToGain(const BandSnapshot& b, const float node_db) {
        return isShelfLike(b.type) ? node_db * 2.f : node_db;
    }

    juce::String typeName(const FilterType type) {
        switch (type) {
        case FilterType::kPeak: return "Bell";
        case FilterType::kLowShelf: return "Low Shelf";
        case FilterType::kLowPass: return "High Cut";
        case FilterType::kHighShelf: return "High Shelf";
        case FilterType::kHighPass: return "Low Cut";
        case FilterType::kNotch: return "Notch";
        case FilterType::kBandPass: return "Band Pass";
        case FilterType::kTiltShelf: return "Tilt Shelf";
        case FilterType::kFlatTilt: return "Flat Tilt";
        case FilterType::kAllPass: return "All Pass";
        case FilterType::kFlatGain: return "Flat Gain";
        default: return {};
        }
    }
}
