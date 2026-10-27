// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "PluginProcessor.hpp"

#include "ui/editor.hpp"

namespace altvane::deesser {
    namespace {
        void storePeak(std::atomic<float>& peak, const float value) {
            auto current = peak.load(std::memory_order::relaxed);
            while (value > current && !peak.compare_exchange_weak(current, value, std::memory_order::relaxed)) {
            }
        }

        int choice(const std::atomic<float>* value) {
            return juce::roundToInt(value->load(std::memory_order::relaxed));
        }
    }

    PluginProcessor::PluginProcessor() :
        AudioProcessor(BusesProperties()
                       .withInput("Input", juce::AudioChannelSet::stereo(), true)
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                       .withInput("Side Chain", juce::AudioChannelSet::stereo(), false)),
        parameters_(*this, &session_.undo_manager, "Parameters", params::createLayout()) {
        bypass_ = dynamic_cast<juce::AudioParameterBool*>(parameters_.getParameter(params::kBypass));
        threshold_ = parameters_.getRawParameterValue(params::kThreshold);
        range_ = parameters_.getRawParameterValue(params::kRange);
        mode_ = parameters_.getRawParameterValue(params::kMode);
        processing_ = parameters_.getRawParameterValue(params::kProcessing);
        shape_ = parameters_.getRawParameterValue(params::kShape);
        band_low_ = parameters_.getRawParameterValue(params::kBandLow);
        band_high_ = parameters_.getRawParameterValue(params::kBandHigh);
        lookahead_ = parameters_.getRawParameterValue(params::kLookahead);
        link_ = parameters_.getRawParameterValue(params::kStereoLink);
        stereo_ = parameters_.getRawParameterValue(params::kStereoMode);
        side_chain_ = parameters_.getRawParameterValue(params::kSideChain);
        output_ = parameters_.getRawParameterValue(params::kOutput);
        parameters_.addParameterListener(params::kLookahead, this);
        // look-ahead is not automatable, but a host may still set it off the message thread: picked up here
        // (posting a message from the audio thread could lock or allocate)
        startTimerHz(10);
    }

    PluginProcessor::~PluginProcessor() {
        parameters_.removeParameterListener(params::kLookahead, this);
        stopTimer();
    }

    dsp::Controls PluginProcessor::readControls(const bool force_bypass) const {
        dsp::Controls c;
        c.threshold_db = threshold_->load(std::memory_order::relaxed);
        c.range_db = range_->load(std::memory_order::relaxed);
        c.mode = static_cast<dsp::Mode>(choice(mode_));
        c.processing = static_cast<dsp::Processing>(choice(processing_));
        c.shape = static_cast<dsp::Shape>(choice(shape_));
        c.band_low = band_low_->load(std::memory_order::relaxed);
        c.band_high = band_high_->load(std::memory_order::relaxed);
        c.band_open = params::isOpen(c.band_high);
        c.lookahead_ms = lookahead_->load(std::memory_order::relaxed);
        c.link = link_->load(std::memory_order::relaxed) * .01f;
        c.stereo = static_cast<dsp::StereoMode>(choice(stereo_));
        c.output_db = output_->load(std::memory_order::relaxed);
        c.bypass = force_bypass || bypass_->get();
        c.listen = getListen();
        return c;
    }

    void PluginProcessor::prepareToPlay(const double sample_rate, const int) {
        engine_.setControls(readControls(false));
        engine_.prepare(sample_rate, 2);
        updateLatency();
    }

    void PluginProcessor::releaseResources() {
    }

    bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())) {
            return false;
        }
        if (layouts.inputBuses.size() > 1) {
            const auto side = layouts.getChannelSet(true, 1);
            return side.isDisabled() || side == juce::AudioChannelSet::mono() || side == juce::AudioChannelSet::stereo();
        }
        return true;
    }

    bool PluginProcessor::hasSideChainInput() const {
        const auto* bus = getBus(true, 1);
        return bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
    }

    void PluginProcessor::updateLatency() {
        const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        setLatencySamples(dsp::Engine::lookaheadSamples(lookahead_->load(), rate));
    }

    void PluginProcessor::parameterChanged(const juce::String&, float) {
        // hosts may only hear about latency on the message thread; changes from elsewhere wait for the timer
        if (juce::MessageManager::existsAndIsCurrentThread()) {
            updateLatency();
        }
    }

    void PluginProcessor::timerCallback() {
        const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        if (dsp::Engine::lookaheadSamples(lookahead_->load(), rate) != getLatencySamples()) {
            updateLatency();
        }
    }

    void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
        process(buffer, false);
    }

    void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
        process(buffer, true);
    }

    void PluginProcessor::process(juce::AudioBuffer<float>& buffer, const bool force_bypass) {
        juce::ScopedNoDenormals no_denormals;
        auto main = getBusBuffer(buffer, true, 0);
        const auto channels = std::min(main.getNumChannels(), 2);
        const auto n = buffer.getNumSamples();
        if (channels == 0 || n == 0) {
            return;
        }
        for (int c = 0; c < channels; ++c) {
            storePeak(input_peak_[static_cast<size_t>(c)], main.getMagnitude(c, 0, n));
        }

        // the side chain's view keeps its channel pointers inside itself: it has to outlive the engine's use of them
        const float* const* side = nullptr;
        int side_channels = 0;
        juce::AudioBuffer<float> side_buffer;
        if (choice(side_chain_) == 1 && hasSideChainInput()) {
            side_buffer = getBusBuffer(buffer, true, 1);
            side_channels = std::min(side_buffer.getNumChannels(), 2);
            side = side_channels > 0 ? side_buffer.getArrayOfReadPointers() : nullptr;
        }
        engine_.setControls(readControls(force_bypass));
        engine_.process(main.getArrayOfWritePointers(), channels, side, side_channels, n);

        for (int c = 0; c < channels; ++c) {
            storePeak(output_peak_[static_cast<size_t>(c)], main.getMagnitude(c, 0, n));
        }
        // anything beyond the main output (e.g. the side chain's channels) is not ours to pass on
        for (auto c = getTotalNumOutputChannels(); c < buffer.getNumChannels(); ++c) {
            buffer.clear(c, 0, n);
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
    return new altvane::deesser::PluginProcessor();
}
