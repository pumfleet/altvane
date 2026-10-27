// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "altvane_ui/session/param_editor.hpp"
#include "../../PluginProcessor.hpp"

namespace altvane::compressor {
    /**
     * the editor's access to the engine's parameters (undoable, through the ParamEditor)
     * and to the display state (non-automatable, saved with the session, not undoable)
     */
    class CompBridge final {
    public:
        CompBridge(PluginProcessor& p, ui::ParamEditor& editor);

        PluginProcessor& getProcessor() const { return p_ref_; }

        ui::ParamEditor& getEditor() const { return editor_; }

        juce::RangedAudioParameter* para(const juce::String& id) const;

        float get(const juce::String& id) const;

        int getChoice(const juce::String& id) const { return juce::roundToInt(get(id)); }

        bool getBool(const juce::String& id) const { return get(id) > .5f; }

        /**
         * set one parameter as one undoable step
         */
        void set(const juce::String& id, float plain, const juce::String& name) const;

        void toggle(const juce::String& id, const juce::String& name) const;

        float getDisplay(const juce::String& id) const;

        int getDisplayChoice(const juce::String& id) const { return juce::roundToInt(getDisplay(id)); }

        void setDisplay(const juce::String& id, float plain) const;

        zlp::PCompDirection::Direction getDirection() const {
            return static_cast<zlp::PCompDirection::Direction>(getChoice(zlp::PCompDirection::kID));
        }

        /**
         * inflate and shape raise the level, so their gain change is drawn upwards and in green
         */
        bool isUpward() const {
            const auto d = getDirection();
            return d == zlp::PCompDirection::kInflate || d == zlp::PCompDirection::kShape;
        }

        /**
         * the engine runs Optical and Vocal as Clean when inflating or expanding
         */
        bool isStyleForcedClean() const;

        /**
         * the displayed dB range of the level display, knee display and meters
         */
        float getScaleDB() const;

        /**
         * any side-chain setting that changes what the detector hears
         */
        bool isSideChainActive() const;

        /**
         * the side-chain channel amounts are labelled by the stereo mode: Mid / Side or Left / Right
         */
        bool isMidSide() const { return getChoice(zlp::PSideStereoMode::kID) % 2 == 0; }

    private:
        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
    };
}
