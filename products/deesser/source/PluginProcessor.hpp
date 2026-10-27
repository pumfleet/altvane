// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <altvane_ui/session/session.hpp>

#include "params.hpp"
#include "dsp/engine.hpp"

namespace altvane::deesser {
    class PluginProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener,
                                  private juce::Timer {
    public:
        // edit history, A/B slots and current preset of the editor, kept so they survive closing the editor
        // (declared first: the parameters record their changes in its undo manager)
        ui::Session session_;
        juce::AudioProcessorValueTreeState parameters_;

        PluginProcessor();

        ~PluginProcessor() override;

        void prepareToPlay(double sample_rate, int samples_per_block) override;

        void releaseResources() override;

        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

        void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;

        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }

        bool acceptsMidi() const override { return false; }

        bool producesMidi() const override { return false; }

        bool isMidiEffect() const override { return false; }

        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }

        int getCurrentProgram() override { return 0; }

        void setCurrentProgram(int) override {
        }

        // hosts list it (a VST3 program list needs a name for its one program)
        const juce::String getProgramName(int) override { return "Default"; }

        void changeProgramName(int, const juce::String&) override {
        }

        void getStateInformation(juce::MemoryBlock& dest_data) override;

        void setStateInformation(const void* data, int size_in_bytes) override;

        juce::AudioProcessorParameter* getBypassParameter() const override { return bypass_; }

        /**
         * what the output plays (dsp::Listen); an audition aid, not saved with the session
         */
        void setListen(dsp::Listen listen) { listen_.store(static_cast<int>(listen)); }

        dsp::Listen getListen() const { return static_cast<dsp::Listen>(listen_.load()); }

        dsp::Engine& getEngine() { return engine_; }

        /**
         * whether the host has connected an external side chain
         */
        bool hasSideChainInput() const;

        /**
         * the input and output peaks since the last call, in dB (message thread)
         */
        std::array<float, 2> takeInputPeaks();

        std::array<float, 2> takeOutputPeaks();

    private:
        dsp::Engine engine_;
        juce::AudioParameterBool* bypass_{nullptr};
        std::atomic<float>* threshold_{nullptr};
        std::atomic<float>* range_{nullptr};
        std::atomic<float>* mode_{nullptr};
        std::atomic<float>* processing_{nullptr};
        std::atomic<float>* shape_{nullptr};
        std::atomic<float>* band_low_{nullptr};
        std::atomic<float>* band_high_{nullptr};
        std::atomic<float>* lookahead_{nullptr};
        std::atomic<float>* link_{nullptr};
        std::atomic<float>* stereo_{nullptr};
        std::atomic<float>* side_chain_{nullptr};
        std::atomic<float>* output_{nullptr};
        std::atomic<int> listen_{0};
        std::array<std::atomic<float>, 2> input_peak_{}, output_peak_{};

        dsp::Controls readControls(bool force_bypass) const;
        void process(juce::AudioBuffer<float>& buffer, bool force_bypass);
        void updateLatency();
        void parameterChanged(const juce::String& id, float value) override;
        void timerCallback() override;
        static std::array<float, 2> takePeaks(std::array<std::atomic<float>, 2>& peaks);
    };
}
