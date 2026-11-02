// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "factory_presets.hpp"

#include "../PluginProcessor.hpp"
#include "../state/preset_json.hpp"

namespace altvane::compressor {
    namespace {
        using Values = std::vector<std::pair<juce::String, float>>;

        struct Preset {
            const char* name;
            // values that differ from the defaults (plain values)
            Values values;
        };

        // zlp::PCompStyle's choices
        constexpr float kClean = 0.f, kClassic = 1.f, kOptical = 2.f, kVocal = 3.f;

        /**
         * a parameter tree with every value at its default
         */
        juce::ValueTree defaults(juce::AudioProcessorValueTreeState& tree) {
            auto state = tree.copyState();
            for (auto child : state) {
                if (auto* para = tree.getParameter(child.getProperty("id").toString())) {
                    child.setProperty("value", para->convertFrom0to1(para->getDefaultValue()), nullptr);
                }
            }
            return state;
        }

        void apply(juce::ValueTree& state, const Values& values) {
            for (const auto& [id, v] : values) {
                auto child = state.getChildWithProperty("id", id);
                jassert(child.isValid());
                child.setProperty("value", v, nullptr);
            }
        }

        /**
         * side-chain EQ band 0 as a 12 dB/oct high-pass, so low end does not drive the detector
         */
        Values sideHighPass(const float freq) {
            return {
                {zlp::PFilterType::kID + juce::String(0), static_cast<float>(zldsp::filter::FilterType::kHighPass)},
                {zlp::POrder::kID + juce::String(0), 1.f},
                {zlp::PFreq::kID + juce::String(0), freq},
                {zlp::PFilterStatus::kID + juce::String(0), 2.f},
            };
        }

        Values join(Values a, const Values& b) {
            a.insert(a.end(), b.begin(), b.end());
            return a;
        }
    }

    ui::PresetCodec presetCodec(PluginProcessor& p) {
        ui::PresetCodec codec;
        codec.write = zlstate::PresetJson::write;
        codec.read = [&p](const juce::File& file, juce::MemoryBlock& state) {
            if (const auto result = zlstate::PresetJson::read(file, state); result.failed()) {
                return result;
            }
            const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
            if (xml == nullptr) {
                return juce::Result::fail("Preset state is invalid");
            }
            // the preset's display tree is replaced by the session's: everything in it is display state
            auto tree = juce::ValueTree::fromXml(*xml);
            const auto display = tree.getChildWithName(zlstate::schema::kNonAutomatableState);
            tree.removeChild(display, nullptr);
            tree.appendChild(p.na_parameters_.copyState(), nullptr);
            state.reset();
            juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), state);
            return juce::Result::ok();
        };
        return codec;
    }

    std::vector<std::pair<juce::String, juce::String>> factoryPresets(PluginProcessor& p) {
        // linked left / right detection: the same gain change on both channels keeps the stereo image
        const Values linked{{zlp::PSideStereoMode::kID, 1.f}, {zlp::PSideStereoLink::kID, 100.f}};
        const std::vector<Preset> presets{
            // fast and deep for room mics and drum parallels: the transients are clamped and the room comes up
            {"Drum Smash", {
                 {zlp::PCompStyle::kID, kClean}, {zlp::PThreshold::kID, -30.f}, {zlp::PRatio::kID, 8.f},
                 {zlp::PKneeW::kID, 4.f}, {zlp::PAttack::kID, 4.f}, {zlp::PRelease::kID, 90.f},
                 {zlp::POutGain::kID, 9.f}
             }},
            // a couple of dB on the drum bus; the side-chain high-pass keeps the kick from pumping the rest
            {"Drum Bus Glue", join({
                 {zlp::PCompStyle::kID, kClassic}, {zlp::PThreshold::kID, -20.f}, {zlp::PRatio::kID, 2.f},
                 {zlp::PKneeW::kID, 10.f}, {zlp::PAttack::kID, 30.f}, {zlp::PRelease::kID, 200.f},
                 {zlp::POutGain::kID, 3.f}
             }, sideHighPass(90.f))},
            // evens out a vocal: soft knee, part RMS detection so it follows the phrase more than the consonants
            {"Vocal Leveler", {
                 {zlp::PCompStyle::kID, kVocal}, {zlp::PThreshold::kID, -26.f}, {zlp::PRatio::kID, 3.f},
                 {zlp::PKneeW::kID, 12.f}, {zlp::PAttack::kID, 12.f}, {zlp::PRelease::kID, 160.f},
                 {zlp::POutGain::kID, 5.f}, {zlp::PRMSON::kID, 1.f}, {zlp::PRMSMix::kID, 40.f}
             }},
            // a low ratio and a wide knee for the mix bus, with slow timing and RMS detection
            {"Mix Bus Gentle", join({
                 {zlp::PCompStyle::kID, kClassic}, {zlp::PThreshold::kID, -16.f}, {zlp::PRatio::kID, 1.6f},
                 {zlp::PKneeW::kID, 14.f}, {zlp::PAttack::kID, 50.f}, {zlp::PRelease::kID, 300.f},
                 {zlp::POutGain::kID, 1.5f}, {zlp::PRMSON::kID, 1.f}, {zlp::PRMSMix::kID, 60.f}
             }, sideHighPass(60.f))},
            // the optical style's programme-dependent release keeps a bass line even without choking the notes
            {"Bass Control", {
                 {zlp::PCompStyle::kID, kOptical}, {zlp::PThreshold::kID, -24.f}, {zlp::PRatio::kID, 4.f},
                 {zlp::PKneeW::kID, 8.f}, {zlp::PAttack::kID, 20.f}, {zlp::PRelease::kID, 140.f},
                 {zlp::POutGain::kID, 4.f}
             }},
            // heavy compression mixed in at 45%: the engine scales the gain change and the makeup by the wet amount
            {"Parallel Smash", {
                 {zlp::PCompStyle::kID, kClean}, {zlp::PThreshold::kID, -36.f}, {zlp::PRatio::kID, 20.f},
                 {zlp::PKneeW::kID, 2.f}, {zlp::PAttack::kID, 1.f}, {zlp::PRelease::kID, 80.f},
                 {zlp::POutGain::kID, 14.f}, {zlp::PWet::kID, 45.f}
             }},
            // brings quiet detail up towards the threshold; the floor and range keep the noise floor where it is
            {"Upward Inflate", {
                 {zlp::PCompDirection::kID, static_cast<float>(zlp::PCompDirection::kInflate)},
                 {zlp::PCompStyle::kID, kClean}, {zlp::PThreshold::kID, -24.f}, {zlp::PRatio::kID, 2.5f},
                 {zlp::PKneeW::kID, 6.f}, {zlp::PAttack::kID, 20.f}, {zlp::PRelease::kID, 220.f},
                 {zlp::PFloor::kID, -90.f}, {zlp::PRange::kID, 12.f}
             }},
            // turns spill and room down between hits, by at most 24 dB, with a short hold so tails are not chopped
            {"Gate-ish Expander", {
                 {zlp::PCompDirection::kID, static_cast<float>(zlp::PCompDirection::kExpand)},
                 {zlp::PCompStyle::kID, kClean}, {zlp::PThreshold::kID, -30.f}, {zlp::PRatio::kID, 3.f},
                 {zlp::PKneeW::kID, 4.f}, {zlp::PAttack::kID, 1.f}, {zlp::PRelease::kID, 120.f},
                 {zlp::PHold::kID, 20.f}, {zlp::PRange::kID, 24.f}
             }},
        };

        std::vector<std::pair<juce::String, juce::String>> result;
        for (const auto& preset : presets) {
            auto parameters = defaults(p.parameters_);
            apply(parameters, linked);
            apply(parameters, preset.values);
            juce::ValueTree state(zlstate::schema::kProcessorState);
            state.appendChild(parameters, nullptr);
            state.appendChild(defaults(p.na_parameters_), nullptr);
            result.emplace_back(juce::String("Factory/") + preset.name, zlstate::PresetJson::toText(state));
        }
        return result;
    }
}
