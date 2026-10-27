// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "PluginProcessor.hpp"

#include "ui/editor.hpp"

namespace altvane::dialogue {
    namespace {
        void storePeak(std::atomic<float>& peak, const float value) {
            auto current = peak.load(std::memory_order::relaxed);
            while (value > current && !peak.compare_exchange_weak(current, value, std::memory_order::relaxed)) {
            }
        }
    }

    PluginProcessor::PluginProcessor() :
        AudioProcessor(BusesProperties()
                       .withInput("Input", juce::AudioChannelSet::stereo(), true)
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                       .withOutput("Voice", juce::AudioChannelSet::stereo(), false)
                       .withOutput("Ambience", juce::AudioChannelSet::stereo(), false)
                       .withOutput("Reverb", juce::AudioChannelSet::stereo(), false)),
        parameters_(*this, &session_.undo_manager, "Parameters", params::createLayout()) {
        bypass_ = dynamic_cast<juce::AudioParameterBool*>(parameters_.getParameter(params::kBypass));
        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            levels_[s] = parameters_.getRawParameterValue(params::kLevelIDs[s]);
            for (size_t n = 0; n < dsp::kSensitivityNodes; ++n) {
                sensitivity_[s][n] = parameters_.getRawParameterValue(params::sensitivityID(s, n));
            }
        }
        quality_ = parameters_.getRawParameterValue(params::kQuality);
        stereo_ = parameters_.getRawParameterValue(params::kStereo);
        output_ = parameters_.getRawParameterValue(params::kOutput);
    }

    PluginProcessor::~PluginProcessor() {
        bridge_.release();
    }

    void PluginProcessor::prepareToPlay(const double sample_rate, const int samples_per_block) {
        max_block_ = std::max(samples_per_block, 1);
        updateControls();
        const auto channels = std::max(getMainBusNumInputChannels(), getMainBusNumOutputChannels());
        stem_outputs_ = false;
        for (int s = 0; s < dsp::kNumStems; ++s) {
            stem_outputs_ = stem_outputs_ || getChannelCountOfBus(false, kFirstStemBus + s) > 0;
        }
        model_ready_.store(bridge_.prepare(sample_rate, channels, max_block_, stem_outputs_));
        setLatencySamples(bridge_.getLatency());
        // the engine writes every stem; one the host has not enabled goes here
        stem_scratch_.setSize(stem_outputs_ ? dsp::kNumStems * 2 : 0, max_block_);

        bypass_mix_.reset(sample_rate, .05);
        bypass_mix_.setCurrentAndTargetValue(bypass_->get() ? 1.f : 0.f);
        output_gain_.reset(sample_rate, .03);
        output_gain_.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(output_->load()));
        residual_gain_.reset(sample_rate, .03);
        residual_gain_.setCurrentAndTargetValue(1.f);
        stem_residual_gain_.reset(sample_rate, .03);
        stem_residual_gain_.setCurrentAndTargetValue(1.f);
    }

    void PluginProcessor::releaseResources() {
        bridge_.release();
    }

    bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())) {
            return false;
        }
        // each stem output is off, has the main output's channels, or is stereo on a mono instance (the stem on both
        // channels: AU enables every bus at its default, stereo, and must still offer mono)
        for (int bus = kFirstStemBus; bus < layouts.outputBuses.size(); ++bus) {
            const auto set = layouts.getChannelSet(false, bus);
            if (!set.isDisabled() && set != out
                && !(out == juce::AudioChannelSet::mono() && set == juce::AudioChannelSet::stereo())) {
                return false;
            }
        }
        return true;
    }

    void PluginProcessor::audioWorkgroupContextChanged(const juce::AudioWorkgroup& workgroup) {
        bridge_.getEngine().setWorkgroup(workgroup);
    }

    void PluginProcessor::updateControls() {
        auto& controls = bridge_.getEngine().getControls();
        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            controls.gains[s].store(params::levelToGain(levels_[s]->load(std::memory_order::relaxed)),
                                    std::memory_order::relaxed);
            for (size_t n = 0; n < dsp::kSensitivityNodes; ++n) {
                controls.sensitivity[s][n].store(sensitivity_[s][n]->load(std::memory_order::relaxed),
                                                 std::memory_order::relaxed);
            }
        }
        controls.quality.store(juce::roundToInt(quality_->load(std::memory_order::relaxed)), std::memory_order::relaxed);
        controls.linked.store(stereo_->load(std::memory_order::relaxed) < .5f, std::memory_order::relaxed);
        controls.listen.store(listen_.load(std::memory_order::relaxed), std::memory_order::relaxed);
    }

    void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
        process(buffer, false);
    }

    void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
        process(buffer, true);
    }

    void PluginProcessor::process(juce::AudioBuffer<float>& buffer, const bool force_bypass) {
        juce::ScopedNoDenormals no_denormals;
        // the main bus's channels come first, then the stem outputs'
        const auto channels = std::min({buffer.getNumChannels(), getMainBusNumOutputChannels(), 2});
        for (auto c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) {
            buffer.clear(c, 0, buffer.getNumSamples());
        }
        dsp::StemOutputs stems{};
        std::array<bool, dsp::kNumStems> stem_enabled{};
        std::array<int, dsp::kNumStems> stem_first{};
        if (stem_outputs_) {
            for (int s = 0; s < dsp::kNumStems; ++s) {
                const auto bus = kFirstStemBus + s;
                const auto first = getChannelCountOfBus(false, bus) >= channels
                                       ? getChannelIndexInProcessBlockBuffer(false, bus, 0)
                                       : -1;
                stem_first[static_cast<size_t>(s)] = first;
                stem_enabled[static_cast<size_t>(s)] = first >= 0 && first + channels <= buffer.getNumChannels();
                for (int c = 0; c < channels; ++c) {
                    stems[static_cast<size_t>(s)][static_cast<size_t>(c)] =
                            stem_enabled[static_cast<size_t>(s)] ? buffer.getWritePointer(first + c)
                                                                 : stem_scratch_.getWritePointer(s * 2 + c);
                }
            }
        }
        updateControls();
        for (int c = 0; c < channels; ++c) {
            storePeak(input_peak_[static_cast<size_t>(c)], buffer.getMagnitude(c, 0, buffer.getNumSamples()));
        }

        // what the 48 kHz round trip loses (above 24 kHz) follows the ambience, the stem it belongs to
        const auto ambience = params::levelToGain(levels_[dsp::kAmbience]->load(std::memory_order::relaxed));
        float residual;
        switch (getListen()) {
            case dsp::Listen::kMix: residual = ambience;
                break;
            case dsp::Listen::kAmbience: residual = 1.f;
                break;
            case dsp::Listen::kRemoved: residual = 1.f - ambience;
                break;
            default: residual = 0.f;
                break;
        }
        residual_gain_.setTargetValue(residual);
        // the stem outputs are the mix's parts, whatever the audition: the ambience stem gets it at its level
        stem_residual_gain_.setTargetValue(ambience);
        bypass_mix_.setTargetValue(force_bypass || bypass_->get() ? 1.f : 0.f);
        output_gain_.setTargetValue(juce::Decibels::decibelsToGain(output_->load(std::memory_order::relaxed)));
        const auto realtime = !isNonRealtime();

        for (int offset = 0; offset < buffer.getNumSamples(); offset += max_block_) {
            const auto n = std::min(max_block_, buffer.getNumSamples() - offset);
            juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(), channels, offset, n);
            dsp::StemOutputs chunk_stems{};
            for (size_t s = 0; s < dsp::kNumStems; ++s) {
                for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
                    chunk_stems[s][c] = stems[s][c] == nullptr ? nullptr
                                                               : stems[s][c] + (stem_enabled[s] ? offset : 0);
                }
            }
            // the residual gains change slowly; one value per chunk is enough
            const auto residual_gain = residual_gain_.getCurrentValue();
            residual_gain_.skip(n);
            const auto stem_residual_gain = stem_residual_gain_.getCurrentValue();
            stem_residual_gain_.skip(n);
            bridge_.process(chunk, realtime, residual_gain, stem_outputs_ ? &chunk_stems : nullptr,
                            stem_residual_gain);
            for (int i = 0; i < n; ++i) {
                const auto mix = bypass_mix_.getNextValue();
                const auto gain = output_gain_.getNextValue();
                for (int c = 0; c < channels; ++c) {
                    auto* y = chunk.getWritePointer(c);
                    const auto dry = bridge_.getDelayedDry(c)[i];
                    y[i] = y[i] * gain * (1.f - mix) + dry * mix;
                }
                // bypassed, the main output carries the input and the stem outputs fall silent
                if (stem_outputs_) {
                    for (size_t s = 0; s < dsp::kNumStems; ++s) {
                        for (size_t c = 0; c < static_cast<size_t>(channels); ++c) {
                            chunk_stems[s][c][i] *= gain * (1.f - mix);
                        }
                    }
                }
            }
        }
        for (int c = 0; c < channels; ++c) {
            storePeak(output_peak_[static_cast<size_t>(c)], buffer.getMagnitude(c, 0, buffer.getNumSamples()));
        }
        // a stereo stem output on a mono instance carries its stem on both channels
        if (channels == 1) {
            for (int s = 0; s < dsp::kNumStems; ++s) {
                const auto first = stem_first[static_cast<size_t>(s)];
                if (stem_enabled[static_cast<size_t>(s)] && getChannelCountOfBus(false, kFirstStemBus + s) > 1
                    && first + 1 < buffer.getNumChannels()) {
                    buffer.copyFrom(first + 1, 0, buffer, first, 0, buffer.getNumSamples());
                }
            }
        }
        // a mono bus may be given a second, scratch channel
        const auto layout_channels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());
        if (channels == 1 && buffer.getNumChannels() > layout_channels) {
            buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
        }
    }

    std::array<float, 2> PluginProcessor::takePeaks(std::array<std::atomic<float>, 2>& peaks) {
        std::array<float, 2> result{};
        for (size_t c = 0; c < 2; ++c) {
            result[c] = juce::Decibels::gainToDecibels(peaks[c].exchange(0.f, std::memory_order::relaxed), -120.f);
        }
        return result;
    }

    std::array<float, 2> PluginProcessor::takeInputPeaks() {
        return takePeaks(input_peak_);
    }

    std::array<float, 2> PluginProcessor::takeOutputPeaks() {
        return takePeaks(output_peak_);
    }

    void PluginProcessor::getStateInformation(juce::MemoryBlock& dest_data) {
        auto state = parameters_.copyState();
        state.setProperty("version", VERSION, nullptr);
        if (const auto xml = state.createXml()) {
            copyXmlToBinary(*xml, dest_data);
        }
    }

    void PluginProcessor::setStateInformation(const void* data, const int size_in_bytes) {
        if (const auto xml = getXmlFromBinary(data, size_in_bytes)) {
            if (xml->hasTagName(parameters_.state.getType())) {
                parameters_.replaceState(juce::ValueTree::fromXml(*xml));
            }
        }
    }

    juce::AudioProcessorEditor* PluginProcessor::createEditor() {
        return new Editor(*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new altvane::dialogue::PluginProcessor();
}
