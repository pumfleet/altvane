// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "PluginProcessor.hpp"

#include "ui/editor.hpp"

namespace altvane::leveler {
    namespace {
        void storePeak(std::atomic<float>& peak, const float value) {
            auto current = peak.load(std::memory_order::relaxed);
            while (value > current && !peak.compare_exchange_weak(current, value, std::memory_order::relaxed)) {
            }
        }

        // the session-only part of the state: automation mode and the rider fader, never in presets
        const juce::Identifier kSessionTag{"Session"};
        const juce::Identifier kModeProperty{"mode"};
        const juce::Identifier kRideProperty{"ride"};
    }

    PluginProcessor::PluginProcessor() :
        AudioProcessor(BusesProperties()
                       .withInput("Input", juce::AudioChannelSet::stereo(), true)
                       .withInput("Music", juce::AudioChannelSet::stereo(), false)
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        parameters_(*this, &session_.undo_manager, "Parameters", params::createLayout()) {
        auto ride = params::createRide();
        ride_ = ride.get();
        addParameter(ride.release());

        bypass_ = dynamic_cast<juce::AudioParameterBool*>(parameters_.getParameter(params::kBypass));
        target_ = parameters_.getRawParameterValue(params::kTarget);
        sensitivity_ = parameters_.getRawParameterValue(params::kSensitivity);
        speed_ = parameters_.getRawParameterValue(params::kSpeed);
        guard_ = parameters_.getRawParameterValue(params::kGuard);
        music_ = parameters_.getRawParameterValue(params::kMusic);
        range_min_ = parameters_.getRawParameterValue(params::kRangeMin);
        range_max_ = parameters_.getRawParameterValue(params::kRangeMax);
        idle_ = parameters_.getRawParameterValue(params::kIdle);
        lookahead_ = parameters_.getRawParameterValue(params::kLookahead);
        output_ = parameters_.getRawParameterValue(params::kOutput);
        updateControls();
        startTimerHz(60);
    }

    PluginProcessor::~PluginProcessor() {
        stopTimer();
        if (gesture_open_) {
            ride_->endChangeGesture();
        }
    }

    void PluginProcessor::prepareToPlay(const double sample_rate, const int samples_per_block) {
        max_block_ = std::max(samples_per_block, 1);
        updateControls();
        engine_.prepare(sample_rate, max_block_);
        setLatencySamples(engine_.getLatency());
        const auto* key = getBusCount(true) > 1 ? getBus(true, 1) : nullptr;
        key_channels_.store(key != nullptr && key->isEnabled() ? std::min(key->getNumberOfChannels(), 2) : 0);

        bypass_mix_.reset(sample_rate, .05);
        bypass_mix_.setCurrentAndTargetValue(bypass_->get() ? 1.f : 0.f);
        output_gain_.reset(sample_rate, .03);
        output_gain_.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(output_->load()));
        prepared_ = true;
    }

    void PluginProcessor::releaseResources() {
        prepared_ = false;
        // a Write pass ends with the processing, even if no stopped transport is ever seen
        playing_.store(false);
    }

    bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())) {
            return false;
        }
        if (layouts.inputBuses.size() > 1) {
            const auto key = layouts.getChannelSet(true, 1);
            return key.isDisabled() || key == juce::AudioChannelSet::mono() || key == juce::AudioChannelSet::stereo();
        }
        return true;
    }

    void PluginProcessor::updateControls() {
        auto& c = engine_.getControls();
        constexpr auto relaxed = std::memory_order::relaxed;
        c.target.store(target_->load(relaxed), relaxed);
        c.sensitivity.store(sensitivity_->load(relaxed), relaxed);
        c.speed.store(speed_->load(relaxed), relaxed);
        c.guard.store(guard_->load(relaxed), relaxed);
        c.music.store(music_->load(relaxed), relaxed);
        c.range_min.store(range_min_->load(relaxed), relaxed);
        c.range_max.store(range_max_->load(relaxed), relaxed);
        c.idle.store(idle_->load(relaxed), relaxed);
        c.lookahead.store(lookahead_->load(relaxed) > .5f, relaxed);
        c.output.store(output_->load(relaxed), relaxed);
        c.ride.store(ride_->get(), relaxed);
    }

    void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
        process(buffer, false);
    }

    void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
        process(buffer, true);
    }

    void PluginProcessor::process(juce::AudioBuffer<float>& buffer, const bool force_bypass) {
        juce::ScopedNoDenormals no_denormals;
        updateControls();
        auto main = getBusBuffer(buffer, true, 0);
        const auto channels = std::min(main.getNumChannels(), 2);
        const float* key[2]{};
        int key_channels = 0;
        if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()) {
            const auto key_buffer = getBusBuffer(buffer, true, 1);
            key_channels = std::min(key_buffer.getNumChannels(), 2);
            for (int c = 0; c < key_channels; ++c) {
                key[c] = key_buffer.getReadPointer(c);
            }
        }

        auto playing = false;
        if (auto* head = getPlayHead(); head != nullptr && !isNonRealtime()) {
            if (const auto position = head->getPosition()) {
                playing = position->getIsPlaying();
            }
        }
        playing_.store(playing, std::memory_order::relaxed);
        blocks_.fetch_add(1, std::memory_order::relaxed);

        const auto bypassed = force_bypass || bypass_->get();
        engine_.getControls().bypass.store(bypassed, std::memory_order::relaxed);
        bypass_mix_.setTargetValue(bypassed ? 1.f : 0.f);
        output_gain_.setTargetValue(juce::Decibels::decibelsToGain(output_->load(std::memory_order::relaxed)));
        for (int offset = 0; offset < main.getNumSamples(); offset += max_block_) {
            const auto n = std::min(max_block_, main.getNumSamples() - offset);
            float* io[2]{};
            const float* key_chunk[2]{};
            for (int c = 0; c < channels; ++c) {
                io[c] = main.getWritePointer(c) + offset;
            }
            for (int c = 0; c < key_channels; ++c) {
                key_chunk[c] = key[c] + offset;
            }
            engine_.process(io, channels, key_channels > 0 ? key_chunk : nullptr, key_channels, n);
            for (int i = 0; i < n; ++i) {
                const auto mix = bypass_mix_.getNextValue();
                const auto gain = output_gain_.getNextValue();
                for (int c = 0; c < channels; ++c) {
                    io[c][i] = io[c][i] * gain * (1.f - mix) + engine_.getDelayedDry(c)[i] * mix;
                }
            }
        }
        for (int c = 0; c < channels; ++c) {
            storePeak(output_peak_[static_cast<size_t>(c)], main.getMagnitude(c, 0, main.getNumSamples()));
        }
    }

    void PluginProcessor::timerCallback() {
        // the lookahead changes the latency; the host hears about it from the message thread, straight from the
        // parameter, so a change made while the host has suspended processing is reported before playback resumes
        if (prepared_.load()) {
            const auto latency = lookahead_->load() > .5f ? dsp::kLookaheadHops * engine_.getHopSamples() : 0;
            if (latency != getLatencySamples()) {
                setLatencySamples(latency);
            }
        }

        // a Write pass: the rider fader moves as if a hand moved it, inside one gesture while the transport plays,
        // so the host records it in write, touch and latch modes alike
        // a host that stops calling back has stopped playing, whatever the last block said
        if (const auto blocks = blocks_.load(std::memory_order::relaxed); blocks != blocks_seen_) {
            blocks_seen_ = blocks;
            idle_ticks_ = 0;
        } else if (idle_ticks_ < kIdleTicks) {
            ++idle_ticks_;
        }
        const auto writing = getMode() == dsp::Mode::kWrite && prepared_.load() && idle_ticks_ < kIdleTicks
                             && playing_.load(std::memory_order::relaxed);
        if (writing) {
            const auto gain = engine_.getGainDb();
            if (!gesture_open_) {
                ride_->beginChangeGesture();
                gesture_open_ = true;
                last_written_ = gain + 1.f;
            }
            if (std::abs(gain - last_written_) >= .05f) {
                ride_->setValueNotifyingHost(ride_->convertTo0to1(gain));
                last_written_ = gain;
            }
        } else if (gesture_open_) {
            ride_->endChangeGesture();
            gesture_open_ = false;
        }
    }

    void PluginProcessor::setMode(const dsp::Mode mode) {
        if (mode == getMode()) {
            return;
        }
        engine_.getControls().mode.store(static_cast<int>(mode));
        // the mode is session state but no parameter: tell the host the session changed
        updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    }

    dsp::Mode PluginProcessor::getMode() const {
        return static_cast<dsp::Mode>(engine_.getControls().mode.load());
    }

    void PluginProcessor::beginManualRide() {
        auto& c = engine_.getControls();
        c.manual_gain.store(engine_.getGainDb());
        c.manual.store(true);
    }

    void PluginProcessor::setManualRide(const float gain_db) {
        engine_.getControls().manual_gain.store(std::clamp(gain_db, -params::kRideLimit, params::kRideLimit));
    }

    void PluginProcessor::endManualRide() {
        auto& c = engine_.getControls();
        // the last value is applied even if the whole ride happened between two hops
        c.manual_pending.store(true);
        c.manual.store(false);
    }

    bool PluginProcessor::isManualRide() const {
        return engine_.getControls().manual.load();
    }

    std::array<float, 2> PluginProcessor::takeOutputPeaks() {
        std::array<float, 2> result{};
        for (size_t c = 0; c < 2; ++c) {
            result[c] = juce::Decibels::gainToDecibels(output_peak_[c].exchange(0.f, std::memory_order::relaxed), -120.f);
        }
        return result;
    }

    void PluginProcessor::getStateInformation(juce::MemoryBlock& dest_data) {
        auto state = parameters_.copyState();
        state.setProperty("version", VERSION, nullptr);
        if (const auto xml = state.createXml()) {
            auto* session = xml->createNewChildElement(kSessionTag.toString());
            session->setAttribute(kModeProperty, static_cast<int>(getMode()));
            session->setAttribute(kRideProperty, static_cast<double>(ride_->get()));
            copyXmlToBinary(*xml, dest_data);
        }
    }

    void PluginProcessor::setStateInformation(const void* data, const int size_in_bytes) {
        const auto xml = getXmlFromBinary(data, size_in_bytes);
        if (xml == nullptr || !xml->hasTagName(parameters_.state.getType())) {
            return;
        }
        // presets carry no session part: a preset never switches a track into Write or moves the fader
        if (const auto* session = xml->getChildByName(kSessionTag)) {
            engine_.getControls().mode.store(std::clamp(session->getIntAttribute(kModeProperty), 0, 2));
            *ride_ = static_cast<float>(session->getDoubleAttribute(kRideProperty));
            xml->removeChildElement(xml->getChildByName(kSessionTag), true);
        }
        parameters_.replaceState(juce::ValueTree::fromXml(*xml));
    }

    juce::AudioProcessorEditor* PluginProcessor::createEditor() {
        return new Editor(*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new altvane::leveler::PluginProcessor();
}
