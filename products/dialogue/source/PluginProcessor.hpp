// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <altvane_ui/session/session.hpp>

#include "params.hpp"
#include "dsp/rate_bridge.hpp"

namespace altvane::dialogue {
    class PluginProcessor final : public juce::AudioProcessor {
    public:
        /**
         * output buses 1-3 carry the voice, ambience and reverb, each at its level and the output gain, so together
         * they add up to the main output; off until the host enables them
         */
        static constexpr int kFirstStemBus = 1;

        juce::AudioProcessorValueTreeState parameters_;
        // edit history, A/B slots and current preset of the editor, kept so they survive closing the editor
        ui::Session session_;

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

        const juce::String getProgramName(int) override { return {}; }

        void changeProgramName(int, const juce::String&) override {
        }

        void getStateInformation(juce::MemoryBlock& dest_data) override;

        void setStateInformation(const void* data, int size_in_bytes) override;

        juce::AudioProcessorParameter* getBypassParameter() const override { return bypass_; }

        void audioWorkgroupContextChanged(const juce::AudioWorkgroup& workgroup) override;

        /**
         * what the output plays (dsp::Listen); an audition aid, not saved with the session
         */
        void setListen(dsp::Listen listen) { listen_.store(static_cast<int>(listen)); }

        dsp::Listen getListen() const { return static_cast<dsp::Listen>(listen_.load()); }

        dsp::Engine& getEngine() { return bridge_.getEngine(); }

        bool isModelReady() const { return model_ready_.load(std::memory_order::relaxed); }

        /**
         * the input and output peaks since the last call, in dB (message thread)
         */
        std::array<float, 2> takeInputPeaks();

        std::array<float, 2> takeOutputPeaks();

    private:
        dsp::RateBridge bridge_;
        juce::AudioParameterBool* bypass_{nullptr};
        std::array<std::atomic<float>*, dsp::kNumStems> levels_{};
        std::array<std::array<std::atomic<float>*, dsp::kSensitivityNodes>, dsp::kNumStems> sensitivity_{};
        std::atomic<float>* quality_{nullptr};
        std::atomic<float>* stereo_{nullptr};
        std::atomic<float>* output_{nullptr};
        std::atomic<int> listen_{0};
        std::atomic<bool> model_ready_{false};

        int max_block_{512};
        bool stem_outputs_{false};
        juce::AudioBuffer<float> stem_scratch_;
        juce::SmoothedValue<float> bypass_mix_, output_gain_, residual_gain_, stem_residual_gain_;
        std::array<std::atomic<float>, 2> input_peak_{}, output_peak_{};

        void updateControls();
        void process(juce::AudioBuffer<float>& buffer, bool force_bypass);
        static std::array<float, 2> takePeaks(std::array<std::atomic<float>, 2>& peaks);
    };
}
