// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "presets.hpp"

#include "params.hpp"

namespace altvane::leveler {
    ui::PresetCodec presetCodec() {
        auto codec = ui::PresetCodec::xmlAsJson();
        codec.write = [](const juce::File& file, const juce::MemoryBlock& state) {
            const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
            if (xml == nullptr) {
                return juce::Result::fail("Invalid state");
            }
            xml->deleteAllChildElementsWithTagName("Session");
            return file.replaceWithText(ui::PresetCodec::toJsonText(*xml))
                       ? juce::Result::ok()
                       : juce::Result::fail("Could not write " + file.getFullPathName());
        };
        codec.includes = [](const juce::AudioProcessorParameter& para) {
            const auto* with_id = dynamic_cast<const juce::AudioProcessorParameterWithID*>(&para);
            return with_id == nullptr || with_id->getParameterID() != params::kRide;
        };
        return codec;
    }
}
