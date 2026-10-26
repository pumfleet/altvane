// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "factory_presets.hpp"

#include <altvane_ui/session/preset_manager.hpp>

#include "../params.hpp"

namespace altvane::dialogue {
    namespace {
        struct Preset {
            const char* name;
            float voice, ambience, reverb;
            // sensitivity nodes that differ from flat: {stem, node, value}
            std::vector<std::tuple<size_t, size_t, float>> sensitivity;
        };

        juce::String toPresetText(const Preset& preset) {
            juce::XmlElement state("Parameters");
            auto add = [&state](const juce::String& id, const float value) {
                auto* param = state.createNewChildElement("PARAM");
                param->setAttribute("id", id);
                param->setAttribute("value", value);
            };
            add(params::kVoice, preset.voice);
            add(params::kAmbience, preset.ambience);
            add(params::kReverb, preset.reverb);
            add(params::kQuality, 1.f);
            add(params::kStereo, 0.f);
            add(params::kOutput, 0.f);
            add(params::kBypass, 0.f);
            for (size_t s = 0; s < dsp::kNumStems; ++s) {
                for (size_t n = 0; n < dsp::kSensitivityNodes; ++n) {
                    auto value = 0.f;
                    for (const auto& [stem, node, v] : preset.sensitivity) {
                        if (stem == s && node == n) {
                            value = v;
                        }
                    }
                    add(params::sensitivityID(s, n), value);
                }
            }
            return ui::PresetCodec::toJsonText(state);
        }
    }

    std::vector<std::pair<juce::String, juce::String>> factoryPresets() {
        using namespace dsp;
        constexpr auto off = params::kLevelOff;
        const std::vector<Preset> presets{
            {"Interview", 0.f, -20.f, -12.f, {}},
            {"Clean Dialogue", 0.f, off, -24.f, {}},
            {"Remove Room", 0.f, -12.f, off, {}},
            {"Keep the Room", 0.f, -8.f, -4.f, {}},
            {"Ambience Only", off, 0.f, off, {}},
            {"Run and Gun", 1.5f, -30.f, -16.f, {{kVoice, 4, .3f}, {kVoice, 5, .3f}}},
            {"Wind and Rumble", 0.f, -36.f, -12.f, {{kAmbience, 0, .7f}, {kAmbience, 1, .4f}}},
            {"Keep Consonants", 0.f, -24.f, -12.f, {{kVoice, 4, .5f}, {kVoice, 5, .6f}}},
        };
        std::vector<std::pair<juce::String, juce::String>> result;
        for (const auto& preset : presets) {
            result.emplace_back(juce::String("Factory/") + preset.name, toPresetText(preset));
        }
        return result;
    }
}
