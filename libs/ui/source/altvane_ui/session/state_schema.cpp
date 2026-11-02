// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "state_schema.hpp"

namespace altvane::ui::state_schema {
    int read(const juce::XmlElement& state) {
        return std::max(state.getIntAttribute(kAttribute, 0), 0);
    }

    void write(juce::XmlElement& state, const int version) {
        state.setAttribute(kAttribute, version);
    }

    int upgrade(juce::XmlElement& state, const int current, const std::span<const Migration> migrations) {
        const auto saved = read(state);
        if (saved > current) {
            return saved;
        }
        // version 0 is version 1's layout, so the migrations start from 1
        auto version = std::max(saved, 1);
        for (const auto& migration : migrations) {
            jassert(migration.from >= 1 && migration.from < current);
            if (migration.from == version) {
                migration.apply(state);
                ++version;
            }
        }
        // anything short of current is a missing migration
        jassert(version == current);
        return saved;
    }

    juce::ValueTree knownParameters(const juce::ValueTree& saved, const juce::AudioProcessorValueTreeState& parameters) {
        juce::ValueTree result(saved.getType());
        for (const auto& child : saved) {
            if (child.hasType("PARAM") && parameters.getParameter(child.getProperty("id").toString()) != nullptr) {
                result.appendChild(child.createCopy(), nullptr);
            }
        }
        return result;
    }

    void settleSwitches(const juce::AudioProcessor& owner) {
        for (auto* para : owner.getParameters()) {
            if (const auto* toggle = dynamic_cast<juce::AudioParameterBool*>(para)) {
                if (const auto settled = toggle->get() ? 1.f : 0.f; !juce::exactlyEqual(para->getValue(), settled)) {
                    para->setValueNotifyingHost(settled);
                }
            }
        }
    }
}
