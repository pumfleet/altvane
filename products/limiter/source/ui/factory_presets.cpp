// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "factory_presets.hpp"

#include "../PluginProcessor.hpp"
#include "common.hpp"
#include "model/character.hpp"

namespace altvane::limiter {
    namespace {
        struct Preset {
            const char* name;
            size_t character;
            // automatable and session values that differ from the defaults (plain values)
            std::vector<std::pair<const char*, float>> values;
            std::vector<std::pair<const char*, float>> session;
        };

        enum Characters : size_t { kTransparent, kBalanced, kPunchy, kAggressive, kSafe };

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

        void apply(juce::ValueTree& state, const std::vector<std::pair<const char*, float>>& values) {
            for (const auto& [id, v] : values) {
                state.getChildWithProperty("id", juce::String(id)).setProperty("value", v, nullptr);
            }
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
            auto tree = juce::ValueTree::fromXml(*xml);
            auto session = tree.getChildWithName(zlstate::schema::kNonAutomatableState);
            const std::array kFromPreset{zlstate::PLoudnessTarget::kID, zlstate::PCustomTargetLUFS::kID,
                                         zlstate::PCustomTargetTruePeak::kID};
            for (auto* para : p.dummy_processor_.getParameters()) {
                const auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(para);
                if (ranged == nullptr || std::any_of(kFromPreset.begin(), kFromPreset.end(), [&](const char* id) {
                    return ranged->getParameterID() == id;
                })) {
                    continue;
                }
                const auto id = ranged->getParameterID();
                auto child = session.getChildWithProperty("id", id);
                if (!child.isValid()) {
                    child = juce::ValueTree("PARAM");
                    child.setProperty("id", id, nullptr);
                    session.appendChild(child, nullptr);
                }
                child.setProperty("value", value(p.na_parameters_, id.toRawUTF8()), nullptr);
            }
            state.reset();
            juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), state);
            return juce::Result::ok();
        };
        return codec;
    }

    std::vector<std::pair<juce::String, juce::String>> factoryPresets(PluginProcessor& p) {
        namespace params = altvane::limiter::params;
        constexpr auto os4 = 2.f, os8 = 3.f;
        const std::vector<Preset> presets{
            {"Streaming Master", kBalanced,
             {{zlp::PInputGain::kID, 4.f}, {zlp::POutputCeiling::kID, -1.f}, {zlp::POversampling::kID, os4}},
             {{zlstate::PLoudnessTarget::kID, 0.f}}},
            {"Transparent Master", kTransparent,
             {{zlp::PInputGain::kID, 3.f}, {zlp::POutputCeiling::kID, -1.f}, {zlp::POversampling::kID, os4}},
             {{zlstate::PLoudnessTarget::kID, 0.f}}},
            {"Loud and Punchy", kPunchy,
             {{zlp::PInputGain::kID, 8.f}, {zlp::POutputCeiling::kID, -.5f}, {zlp::POversampling::kID, os8}},
             {{zlstate::PLoudnessTarget::kID, 6.f}}},
            {"Apple Music Master", kBalanced,
             {{zlp::PInputGain::kID, 3.f}, {zlp::POutputCeiling::kID, -1.f}, {zlp::POversampling::kID, os4}},
             {{zlstate::PLoudnessTarget::kID, 1.f}}},
            {"Broadcast EBU R128", kSafe,
             {{zlp::POutputCeiling::kID, -1.f}},
             {{zlstate::PLoudnessTarget::kID, 3.f}, {zlstate::PLoudnessScale::kID, 1.f}}},
            {"Podcast Voice", kTransparent,
             {{zlp::PInputGain::kID, 6.f}, {zlp::POutputCeiling::kID, -1.f}},
             {{zlstate::PLoudnessTarget::kID, 5.f}}},
            {"CD Master 16 bit", kTransparent,
             {{zlp::PInputGain::kID, 4.f}, {zlp::POutputCeiling::kID, -.3f}, {zlp::POversampling::kID, os4},
              {params::PDitherBits::kID, 3.f}, {params::PDitherShape::kID, 1.f}},
             {{zlstate::PLoudnessTarget::kID, 6.f}}},
            {"Safety Limiter", kSafe,
             {{zlp::POutputCeiling::kID, -.3f}},
             {{zlstate::PLoudnessTarget::kID, 6.f}}},
            {"Drum Bus", kAggressive,
             {{zlp::PInputGain::kID, 6.f}, {zlp::POutputCeiling::kID, -1.f}, {zlp::PTruePeak::kID, 0.f},
              {zlp::POversampling::kID, 1.f}},
             {{zlstate::PLoudnessTarget::kID, 6.f}}},
        };

        std::vector<std::pair<juce::String, juce::String>> result;
        for (const auto& preset : presets) {
            auto parameters = defaults(p.parameters_);
            const auto timing = characterValues(kCharacters[preset.character]);
            for (size_t k = 0; k < timing.size(); ++k) {
                apply(parameters, {{kCharacterParameterIDs[k], timing[k]}});
            }
            apply(parameters, preset.values);
            auto session = defaults(p.na_parameters_);
            apply(session, preset.session);

            juce::ValueTree state(zlstate::schema::kProcessorState);
            state.appendChild(parameters, nullptr);
            state.appendChild(session, nullptr);
            result.emplace_back(juce::String("Factory/") + preset.name, zlstate::PresetJson::toText(state));
        }
        return result;
    }
}
