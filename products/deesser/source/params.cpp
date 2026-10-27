// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "params.hpp"

#include <altvane_ui/util/value_format.hpp>

namespace altvane::deesser::params {
    namespace {
        constexpr int kVersion = 1;

        float parseNumber(const juce::String& t) {
            return t.replace(juce::String::fromUTF8("\xe2\x88\x92"), "-").retainCharacters("+-.0123456789").getFloatValue();
        }

        float parseHz(const juce::String& t) {
            if (const auto hz = ui::format::parseFreq(t)) {
                return static_cast<float>(*hz);
            }
            return parseNumber(t);
        }

        /**
         * a log-frequency range (equal travel per octave)
         */
        juce::NormalisableRange<float> logRange(const float lo, const float hi) {
            return {lo, hi,
                    [](const float start, const float end, const float t) { return start * std::pow(end / start, t); },
                    [](const float start, const float end, const float v) {
                        return std::log(v / start) / std::log(end / start);
                    },
                    [](const float start, const float end, const float v) {
                        return std::clamp(static_cast<float>(juce::roundToInt(v)), start, end);
                    }};
        }
    }

    juce::String formatDb(const float db) {
        // the typographic minus, as everywhere else in the interface
        const auto sign = db < -.04f ? juce::String::fromUTF8("\xe2\x88\x92") : juce::String();
        return sign + juce::String(std::abs(db), 1) + " dB";
    }

    juce::String formatHz(const float hz) {
        return hz >= 1000.f ? juce::String(hz / 1000.f, hz >= 10000.f ? 1 : 2) + " kHz"
                            : juce::String(juce::roundToInt(hz)) + " Hz";
    }

    juce::String formatBandHigh(const float hz) {
        return isOpen(hz) ? juce::String("Open") : formatHz(hz);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const auto db_attributes = juce::AudioParameterFloatAttributes()
                                   .withStringFromValueFunction([](const float v, int) { return formatDb(v); })
                                   .withValueFromStringFunction([](const juce::String& t) { return parseNumber(t); })
                                   .withLabel("dB");
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kThreshold, kVersion}, "Threshold",
            juce::NormalisableRange<float>(kThresholdMin, kThresholdMax, .1f), kThresholdDefault, db_attributes));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kRange, kVersion}, "Range",
            juce::NormalisableRange<float>(0.f, kRangeMax, .1f), kRangeDefault, db_attributes));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{kMode, kVersion}, "Mode", kModeChoices, 0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{kProcessing, kVersion}, "Processing", kProcessingChoices, 1));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{kShape, kVersion}, "Shape", kShapeChoices, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kBandLow, kVersion}, "Band Low", logRange(kBandLowMin, kBandLowMax), kBandLowDefault,
            juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](const float v, int) { return formatHz(v); })
            .withValueFromStringFunction([](const juce::String& t) { return parseHz(t); })
            .withLabel("Hz")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kBandHigh, kVersion}, "Band High", logRange(kBandHighMin, kBandHighMax), kBandHighMax,
            juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](const float v, int) { return formatBandHigh(v); })
            .withValueFromStringFunction([](const juce::String& t) {
                return t.trim().equalsIgnoreCase("open") ? kBandHighMax : parseHz(t);
            })
            .withLabel("Hz")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kLookahead, kVersion}, "Lookahead",
            juce::NormalisableRange<float>(0.f, kLookaheadMax, .1f), kLookaheadDefault,
            juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](const float v, int) { return ui::format::ms(v); })
            .withValueFromStringFunction([](const juce::String& t) { return parseNumber(t); })
            .withLabel("ms")));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kStereoLink, kVersion}, "Stereo Link",
            juce::NormalisableRange<float>(0.f, 100.f, 1.f), 100.f,
            juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](const float v, int) { return ui::format::percent(v); })
            .withValueFromStringFunction([](const juce::String& t) { return parseNumber(t); })
            .withLabel("%")));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{kStereoMode, kVersion}, "Stereo Mode", kStereoChoices, 0));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{kSideChain, kVersion}, "Side Chain", kSideChainChoices, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{kOutput, kVersion}, "Output",
            juce::NormalisableRange<float>(-24.f, 12.f, .1f), 0.f, db_attributes));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{kBypass, kVersion}, "Bypass", false));
        return layout;
    }

    juce::StringArray allIDs() {
        return {
            kThreshold, kRange, kMode, kProcessing, kShape, kBandLow, kBandHigh, kLookahead, kStereoLink,
            kStereoMode, kSideChain, kOutput, kBypass
        };
    }
}
