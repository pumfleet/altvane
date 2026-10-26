// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "params.hpp"

namespace altvane::dialogue::params {
    namespace {
        constexpr int kVersion = 1;

        juce::String formatSensitivity(const float v) {
            const auto percent = juce::roundToInt(v * 100.f);
            return (percent > 0 ? "+" : "") + juce::String(percent) + "%";
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const auto level_attributes = juce::AudioParameterFloatAttributes()
                                      .withStringFromValueFunction([](const float v, int) { return formatLevel(v); })
                                      .withValueFromStringFunction([](const juce::String& t) {
                                          if (t.trim().equalsIgnoreCase("off")) {
                                              return kLevelOff;
                                          }
                                          return t.retainCharacters("+-.0123456789").getFloatValue();
                                      })
                                      .withLabel("dB");
        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{kLevelIDs[s], kVersion}, kStemNames[s],
                juce::NormalisableRange<float>(kLevelOff, kLevelMax, .1f), kLevelDefaults[s], level_attributes));
        }
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{kQuality, kVersion}, "Quality", kQualityChoices, 1));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{kStereo, kVersion}, "Stereo", kStereoChoices, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kOutput, kVersion}, "Output", juce::NormalisableRange<float>(-24.f, 12.f, .1f), 0.f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{kBypass, kVersion}, "Bypass", false));

        const auto sensitivity_attributes = juce::AudioParameterFloatAttributes()
                                            .withStringFromValueFunction([](const float v, int) {
                                                return formatSensitivity(v);
                                            })
                                            .withValueFromStringFunction([](const juce::String& t) {
                                                return t.retainCharacters("+-.0123456789").getFloatValue() / 100.f;
                                            });
        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            for (size_t n = 0; n < dsp::kSensitivityNodes; ++n) {
                const auto hz = dsp::kSensitivityFrequencies[n];
                const auto freq = hz >= 1000.f ? juce::String(hz / 1000.f, 1) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID{sensitivityID(s, n), kVersion},
                    juce::String(kStemNames[s]) + " Sensitivity " + freq,
                    juce::NormalisableRange<float>(-1.f, 1.f, .01f), 0.f, sensitivity_attributes));
            }
        }
        return layout;
    }

    juce::StringArray allIDs() {
        juce::StringArray ids{kVoice, kAmbience, kReverb, kQuality, kStereo, kOutput, kBypass};
        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            for (size_t n = 0; n < dsp::kSensitivityNodes; ++n) {
                ids.add(sensitivityID(s, n));
            }
        }
        return ids;
    }
}
