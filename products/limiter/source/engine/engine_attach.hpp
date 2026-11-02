// Copyright (C) 2026 - Altvane, based on ZLLimiter by zsliu98
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <array>
#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../zlp/controller.hpp"
#include "../zlp/zlp_definitions.hpp"

namespace altvane::limiter {
    /**
     * upstream's zlp::LimiterAttach (source/zlp/limiter_attach.*) without oversampling: it hands the engine's
     * parameters to the zlp::Controller as they change, on whichever thread changes them
     *
     * the controller switches oversampling inside process() and then posts a message (triggerAsyncUpdate) to report
     * the new latency, which must not happen on the audio thread; PluginProcessor applies oversampling itself, off
     * the audio thread. Keep this in step with upstream's LimiterAttach on every sync (UPSTREAM watches it): a test
     * checks that every engine parameter but oversampling is listed here
     */
    class EngineAttach final : private juce::AudioProcessorValueTreeState::Listener {
    public:
        static constexpr std::array kIDs{
            zlp::PInputGain::kID, zlp::PInputOutputLink::kID, zlp::POutputCeiling::kID, zlp::PTruePeak::kID,
            zlp::PLookahead::kID, zlp::PAttack::kID, zlp::PRelease::kID, zlp::PChannelDelta::kID,
            zlp::PBypass::kID, zlp::PDelta::kID, zlp::PRecovery::kID,
        };

        EngineAttach(juce::AudioProcessorValueTreeState& parameters, zlp::Controller& controller) :
            parameters_ref_(parameters), controller_ref_(controller) {
            for (const auto* id : kIDs) {
                parameters_ref_.addParameterListener(id, this);
                parameterChanged(id, parameters_ref_.getRawParameterValue(id)->load(std::memory_order::relaxed));
            }
        }

        ~EngineAttach() override {
            for (const auto* id : kIDs) {
                parameters_ref_.removeParameterListener(id, this);
            }
        }

    private:
        juce::AudioProcessorValueTreeState& parameters_ref_;
        zlp::Controller& controller_ref_;

        void parameterChanged(const juce::String& parameter_id, const float value) override {
            if (parameter_id == zlp::PInputGain::kID) {
                controller_ref_.setInputGain(value);
            } else if (parameter_id == zlp::PInputOutputLink::kID) {
                controller_ref_.setInputOutputLink(value > 0.5f);
            } else if (parameter_id == zlp::POutputCeiling::kID) {
                controller_ref_.setOutputCeiling(value);
            } else if (parameter_id == zlp::PTruePeak::kID) {
                controller_ref_.setTruePeakEnabled(value > 0.5f);
            } else if (parameter_id == zlp::PLookahead::kID) {
                controller_ref_.setLookahead(value);
            } else if (parameter_id == zlp::PAttack::kID) {
                controller_ref_.setAttack(value);
            } else if (parameter_id == zlp::PRelease::kID) {
                controller_ref_.setRelease(value);
            } else if (parameter_id == zlp::PRecovery::kID) {
                controller_ref_.setRecoveryMilliseconds(value);
            } else if (parameter_id == zlp::PChannelDelta::kID) {
                controller_ref_.setChannelDelta(value);
            } else if (parameter_id == zlp::PBypass::kID) {
                controller_ref_.setBypassEnabled(value > 0.5f);
            } else if (parameter_id == zlp::PDelta::kID) {
                controller_ref_.setDeltaEnabled(value > 0.5f);
            }
        }
    };
}
