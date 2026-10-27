// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "factory_presets.hpp"

#include <altvane_ui/session/preset_manager.hpp>

#include "../params.hpp"

namespace altvane::deesser {
    namespace {
        enum Mode { kVocal, kAllround };

        enum Band { kWide, kShelf, kBell };

        struct Preset {
            const char* name;
            float threshold, range;
            Mode mode;
            Band band;
            float low, high;
            float lookahead{params::kLookaheadDefault};
        };

        juce::String toPresetText(const Preset& preset) {
            juce::XmlElement state("Parameters");
            auto add = [&state](const juce::String& id, const float value) {
                auto* param = state.createNewChildElement("PARAM");
                param->setAttribute("id", id);
                param->setAttribute("value", value);
            };
            add(params::kThreshold, preset.threshold);
            add(params::kRange, preset.range);
            add(params::kMode, static_cast<float>(preset.mode));
            add(params::kProcessing, preset.band == kWide ? 0.f : 1.f);
            add(params::kShape, preset.band == kBell ? 1.f : 0.f);
            add(params::kBandLow, preset.low);
            add(params::kBandHigh, preset.high);
            add(params::kLookahead, preset.lookahead);
            add(params::kStereoLink, 100.f);
            add(params::kStereoMode, 0.f);
            add(params::kSideChain, 0.f);
            add(params::kOutput, 0.f);
            add(params::kBypass, 0.f);
            return ui::PresetCodec::toJsonText(state);
        }
    }

    std::vector<std::pair<juce::String, juce::String>> factoryPresets() {
        constexpr auto open = params::kBandHighMax;
        const std::vector<Preset> presets{
            {"Vocal - Gentle", -26.f, 6.f, kVocal, kShelf, 5500.f, open},
            {"Vocal - Firm", -32.f, 12.f, kVocal, kShelf, 5000.f, open},
            {"Female Vocal", -28.f, 9.f, kVocal, kBell, 6000.f, 11000.f},
            {"Male Vocal", -28.f, 9.f, kVocal, kBell, 4000.f, 8000.f},
            {"Podcast and Dialogue", -30.f, 8.f, kVocal, kWide, 4500.f, 10000.f},
            {"Allround - Bus", -22.f, 4.f, kAllround, kShelf, 6000.f, open},
            {"Cymbal Tamer", -24.f, 6.f, kAllround, kShelf, 8000.f, open},
            {"Harsh Guitars", -26.f, 5.f, kAllround, kBell, 3000.f, 6000.f},
        };
        std::vector<std::pair<juce::String, juce::String>> result;
        for (const auto& preset : presets) {
            result.emplace_back(juce::String("Factory/") + preset.name, toPresetText(preset));
        }
        return result;
    }
}
