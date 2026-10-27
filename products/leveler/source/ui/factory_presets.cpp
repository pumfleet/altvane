// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "factory_presets.hpp"

#include <altvane_ui/session/preset_manager.hpp>

#include "../params.hpp"

namespace altvane::leveler {
    namespace {
        struct Preset {
            const char* name;
            float target, sensitivity, speed, guard, range_min, range_max, idle;
            bool lookahead{true};
        };

        juce::String toPresetText(const Preset& preset) {
            juce::XmlElement state("Parameters");
            auto add = [&state](const juce::String& id, const float value) {
                auto* param = state.createNewChildElement("PARAM");
                param->setAttribute("id", id);
                param->setAttribute("value", value);
            };
            add(params::kTarget, preset.target);
            add(params::kSensitivity, preset.sensitivity);
            add(params::kSpeed, preset.speed);
            add(params::kGuard, preset.guard);
            add(params::kMusic, 0.f);
            add(params::kRangeMin, preset.range_min);
            add(params::kRangeMax, preset.range_max);
            add(params::kIdle, preset.idle);
            add(params::kLookahead, preset.lookahead ? 1.f : 0.f);
            add(params::kOutput, 0.f);
            add(params::kBypass, 0.f);
            return ui::PresetCodec::toJsonText(state);
        }
    }

    std::vector<std::pair<juce::String, juce::String>> factoryPresets() {
        //  name                  target  sens   speed  guard  min    max   idle   lookahead
        const std::vector<Preset> presets{
            {"Lead Vocal", -18.f, 0.f, .5f, .7f, -6.f, 6.f, 0.f},
            {"Background Vocals", -24.f, 0.f, .35f, .8f, -6.f, 4.f, -2.f},
            {"Rap", -17.f, 2.f, .7f, .6f, -6.f, 6.f, 0.f},
            {"Podcast", -20.f, 3.f, .45f, .85f, -9.f, 9.f, 0.f},
            {"Voice-over", -18.f, 0.f, .6f, .9f, -12.f, 12.f, -3.f},
            {"Live", -18.f, 0.f, .55f, .7f, -6.f, 6.f, 0.f, false},
            {"Gentle", -20.f, 0.f, .2f, .7f, -3.f, 3.f, 0.f},
            {"Tight", -18.f, 0.f, .85f, .8f, -12.f, 12.f, 0.f},
        };
        std::vector<std::pair<juce::String, juce::String>> result;
        for (const auto& preset : presets) {
            result.emplace_back(juce::String("Factory/") + preset.name, toPresetText(preset));
        }
        return result;
    }
}
