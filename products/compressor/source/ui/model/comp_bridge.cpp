// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "comp_bridge.hpp"

namespace altvane::compressor {
    CompBridge::CompBridge(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
    }

    juce::RangedAudioParameter* CompBridge::para(const juce::String& id) const {
        auto* para = p_ref_.parameters_.getParameter(id);
        jassert(para != nullptr);
        return para;
    }

    float CompBridge::get(const juce::String& id) const {
        const auto* value = p_ref_.parameters_.getRawParameterValue(id);
        jassert(value != nullptr);
        return value != nullptr ? value->load(std::memory_order::relaxed) : 0.f;
    }

    void CompBridge::set(const juce::String& id, const float plain, const juce::String& name) const {
        editor_.setNow({{para(id), plain}}, "Change " + name);
    }

    void CompBridge::toggle(const juce::String& id, const juce::String& name) const {
        editor_.toggle(para(id), name);
    }

    float CompBridge::getDisplay(const juce::String& id) const {
        const auto* value = p_ref_.na_parameters_.getRawParameterValue(id);
        jassert(value != nullptr);
        return value != nullptr ? value->load(std::memory_order::relaxed) : 0.f;
    }

    void CompBridge::setDisplay(const juce::String& id, const float plain) const {
        if (auto* p = p_ref_.na_parameters_.getParameter(id)) {
            ui::ParamEditor::setNotifyingHost(p, p->convertTo0to1(plain));
        }
    }

    bool CompBridge::isStyleForcedClean() const {
        const auto d = getDirection();
        return (d == zlp::PCompDirection::kInflate || d == zlp::PCompDirection::kExpand)
               && getChoice(zlp::PCompStyle::kID) > 1;
    }

    float CompBridge::getScaleDB() const {
        return -zlstate::PAnalyzerMinDB::getDBFromIndex(getDisplay(zlstate::PAnalyzerMinDB::kID));
    }

    bool CompBridge::isSideChainActive() const {
        if (getBool(zlp::PExtSide::kID) || std::abs(get(zlp::PSideGain::kID)) > .01f
            || getChoice(zlp::PSideStereoSwap::kID) != 0
            || get(zlp::PSideStereoWet1::kID) < 99.95f || get(zlp::PSideStereoWet2::kID) < 99.95f) {
            return true;
        }
        if (getBool(zlp::PSideEQBypass::kID)) {
            return false;
        }
        for (size_t i = 0; i < zlp::kBandNum; ++i) {
            if (getChoice(zlp::PFilterStatus::kID + juce::String(i)) == 2) {
                return true;
            }
        }
        return false;
    }
}
