// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "altvane_ui/theme/look_and_feel.hpp"
#include "common.hpp"

class PluginProcessor;

namespace altvane::compressor {
    /**
     * the Altvane Compressor editor
     * placeholder until the designed editor lands (products/compressor/docs/design): the Altvane header over the
     * host-style list of every parameter, so the engine can be auditioned and validated in hosts meanwhile
     */
    class Editor final : public juce::AudioProcessorEditor {
    public:
        explicit Editor(PluginProcessor& p);

        ~Editor() override;

        void paint(juce::Graphics& g) override;

        void resized() override;

    private:
        LookAndFeel laf_;
        juce::GenericAudioProcessorEditor parameters_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
    };
}
