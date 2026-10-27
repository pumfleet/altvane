// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <altvane_ui/session/session.hpp>

#include "params.hpp"
#include "dsp/engine.hpp"

namespace altvane::leveler {
    class PluginProcessor final : public juce::AudioProcessor, private juce::Timer {
    public:
        // edit history, A/B slots and current preset of the editor, kept so they survive closing the editor
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

        const juce::String getProgramName(int) override { return "Default"; }

        void changeProgramName(int, const juce::String&) override {
        }

        void getStateInformation(juce::MemoryBlock& dest_data) override;

        void setStateInformation(const void* data, int size_in_bytes) override;

        juce::AudioProcessorParameter* getBypassParameter() const override { return bypass_; }

        /**
         * the rider fader, the one parameter outside the tree
         */
        juce::AudioParameterFloat* getRide() const { return ride_; }

        dsp::Engine& getEngine() { return engine_; }

        /**
         * automation mode; part of the session, not of presets (a change tells the host the session changed)
         */
        void setMode(dsp::Mode mode);

        dsp::Mode getMode() const;

        /**
         * a hand on the rider fader (Off and Write; in Read the fader edits the automation instead)
         */
        void beginManualRide();

        void setManualRide(float gain_db);

        void endManualRide();

        bool isManualRide() const;

        /**
         * whether a Write pass is sending the rider fader to the host right now
         */
        bool isWritingAutomation() const { return gesture_open_; }

        /**
         * channels on the music key input, 0 when it is not connected
         */
        int getKeyChannels() const { return key_channels_.load(std::memory_order::relaxed); }

        /**
         * the output peaks since the last call, in dB (message thread)
         */
        std::array<float, 2> takeOutputPeaks();

        /**
         * the message-thread work the timer does (latency changes, automation writes), for tests
         */
        void runTimerForTesting() { timerCallback(); }

    private:
        dsp::Engine engine_;
        juce::AudioParameterBool* bypass_{nullptr};
        juce::AudioParameterFloat* ride_{nullptr};
        std::atomic<float>* target_{nullptr};
        std::atomic<float>* sensitivity_{nullptr};
        std::atomic<float>* speed_{nullptr};
        std::atomic<float>* guard_{nullptr};
        std::atomic<float>* music_{nullptr};
        std::atomic<float>* range_min_{nullptr};
        std::atomic<float>* range_max_{nullptr};
        std::atomic<float>* idle_{nullptr};
        std::atomic<float>* lookahead_{nullptr};
        std::atomic<float>* output_{nullptr};

        int max_block_{512};
        std::atomic<bool> prepared_{false};
        juce::SmoothedValue<float> bypass_mix_, output_gain_;
        std::array<std::atomic<float>, 2> output_peak_{};
        std::atomic<int> key_channels_{0};
        std::atomic<bool> playing_{false};

        std::atomic<uint64_t> blocks_{0};

        // message thread: the Write pass
        static constexpr int kIdleTicks = 15; // 250 ms without an audio block
        bool gesture_open_{false};
        float last_written_{0.f};
        uint64_t blocks_seen_{0};
        int idle_ticks_{0};

        void updateControls();
        void process(juce::AudioBuffer<float>& buffer, bool force_bypass);
        void timerCallback() override;
    };
}
