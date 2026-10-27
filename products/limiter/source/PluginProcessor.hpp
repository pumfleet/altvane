// Copyright (C) 2026 - zsliu98
// This file is part of ZLLimiter
//
// ZLLimiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLLimiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLLimiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <optional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <altvane_ui/session/session.hpp>

#include "zlp/zlp.hpp"
#include "state/state.hpp"
#include "engine/dc_filter.hpp"
#include "engine/dither.hpp"
#include "engine/loudness_meter.hpp"
#include "engine/params.hpp"

/**
 * ZL Limiter's processor, adapted for Altvane Limiter: the Altvane editor and session, and an output stage
 * after the limiter (DC filter before it, dither and a loudness meter after it)
 */
class PluginProcessor final : public juce::AudioProcessor {
public:
    zlstate::DummyProcessor dummy_processor_;
    juce::AudioProcessorValueTreeState parameters_;
    juce::AudioProcessorValueTreeState na_parameters_;
    // edit history, A/B slots and current preset of the Altvane Limiter editor, kept so they survive closing it
    altvane::ui::Session session_;

    PluginProcessor();

    ~PluginProcessor() override;

    void prepareToPlay(double sample_rate, int samples_per_block) override;

    void releaseResources() override;

    void reset() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;

    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;

    bool producesMidi() const override;

    bool isMidiEffect() const override;

    double getTailLengthSeconds() const override;

    int getNumPrograms() override;

    int getCurrentProgram() override;

    void setCurrentProgram(int index) override;

    const juce::String getProgramName(int index) override;

    void changeProgramName(int index, const juce::String& new_name) override;

    void getStateInformation(juce::MemoryBlock& dest_data) override;

    void setStateInformation(const void* data, int size_in_bytes) override;

    bool supportsDoublePrecisionProcessing() const override { return false; }

    zlp::Controller& getController() {
        return controller_;
    }

    double getAtomicSampleRate() const {
        return sample_rate_.load(std::memory_order::relaxed);
    }

    /**
     * the output loudness, measured whether or not the editor is open (one consumer: the message thread)
     */
    const altvane::limiter::LoudnessReadings& pullLoudness() { return loudness_meter_.pull(); }

    void resetLoudness() { loudness_meter_.requestReset(); }

    /**
     * the number of channels the limiter runs on (1 or 2), 0 before the first prepare
     */
    int getNumMainChannels() const { return num_main_channels_.load(std::memory_order::relaxed); }

private:
    zlp::Controller controller_;
    zlp::LimiterAttach limiter_attach_;

    std::atomic<double> sample_rate_{48000.0};

    enum ChannelLayout {
        kMain1, kMain2, kInvalid
    };

    ChannelLayout channel_layout_{kInvalid};
    std::atomic<int> num_main_channels_{0};

    bool update_channel_layout_per_call_{false};
    size_t max_block_size_{1};

    // Altvane: the output stage around the limiter
    altvane::limiter::DCFilter dc_filter_;
    altvane::limiter::Dither dither_;
    altvane::limiter::LoudnessMeter loudness_meter_;
    std::atomic<float> &bypass_, &delta_, &dc_filter_on_, &dither_bits_, &dither_shape_, &loudness_auto_reset_;
    bool dc_filter_was_on_{false};
    std::optional<bool> was_playing_;

    void updateChannelLayout();

    void updateLoudnessTransport();

    void processBlockInternal(juce::AudioBuffer<float>& buffer, bool bypass);

    void processPiece(std::span<float*> main, size_t num_samples, bool bypass);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
