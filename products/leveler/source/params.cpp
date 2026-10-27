// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "params.hpp"

namespace altvane::leveler::params {
    namespace {
        constexpr int kVersion = 1;

        float parseNumber(const juce::String& t) {
            return t.replace(juce::String::fromUTF8("\xe2\x88\x92"), "-").retainCharacters("+-.0123456789").getFloatValue();
        }

        juce::AudioParameterFloatAttributes dbAttributes() {
            return juce::AudioParameterFloatAttributes()
                   .withStringFromValueFunction([](const float v, int) { return formatDb(v); })
                   .withValueFromStringFunction([](const juce::String& t) { return parseNumber(t); })
                   .withLabel("dB");
        }

        juce::AudioParameterFloatAttributes percentAttributes() {
            return juce::AudioParameterFloatAttributes()
                   .withStringFromValueFunction([](const float v, int) { return formatPercent(v); })
                   .withValueFromStringFunction([](const juce::String& t) { return parseNumber(t) / 100.f; })
                   .withLabel("%");
        }

        void addFloat(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id, const char* name,
                      const float lo, const float hi, const float step, const float def,
                      const juce::AudioParameterFloatAttributes& attributes) {
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{id, kVersion}, name, juce::NormalisableRange<float>(lo, hi, step), def, attributes));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        addFloat(layout, kTarget, "Target", kTargetMin, kTargetMax, .1f, kTargetDefault, dbAttributes());
        addFloat(layout, kSensitivity, "Sensitivity", -12.f, 12.f, .1f, 0.f, dbAttributes());
        addFloat(layout, kSpeed, "Speed", 0.f, 1.f, .01f, .5f, percentAttributes());
        addFloat(layout, kGuard, "Guard", 0.f, 1.f, .01f, .7f, percentAttributes());
        addFloat(layout, kMusic, "Music", 0.f, 1.f, .01f, 0.f, percentAttributes());
        addFloat(layout, kRangeMin, "Range Min", -kRangeLimit, 0.f, .1f, -6.f, dbAttributes());
        addFloat(layout, kRangeMax, "Range Max", 0.f, kRangeLimit, .1f, 6.f, dbAttributes());
        addFloat(layout, kIdle, "Idle", -kRangeLimit, kRangeLimit, .1f, 0.f, dbAttributes());
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{kLookahead, kVersion}, "Lookahead", true));
        addFloat(layout, kOutput, "Output", -24.f, 24.f, .1f, 0.f, dbAttributes());
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{kBypass, kVersion}, "Bypass", false));
        return layout;
    }

    std::unique_ptr<juce::AudioParameterFloat> createRide() {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kRide, kVersion}, "Rider", juce::NormalisableRange<float>(-kRideLimit, kRideLimit, .01f),
            0.f, dbAttributes());
    }

    juce::StringArray allIDs() {
        return {kTarget, kSensitivity, kSpeed, kGuard, kMusic, kRangeMin, kRangeMax, kIdle, kLookahead, kOutput, kBypass};
    }
}
