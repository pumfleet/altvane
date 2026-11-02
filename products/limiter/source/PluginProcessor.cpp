// Copyright (C) 2026 - zsliu98
// This file is part of ZLLimiter
//
// ZLLimiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLLimiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLLimiter. If not, see <https://www.gnu.org/licenses/>.


#include "PluginProcessor.hpp"

#include <array>
#include <thread>

#include "ui/editor.hpp"

namespace {
    std::atomic<float>& raw(juce::AudioProcessorValueTreeState& tree, const char* id) {
        return *tree.getRawParameterValue(id);
    }

    /**
     * a copy of a saved tree with every parameter it lacks at its default: replaceState() would otherwise keep their
     * current values, e.g. Altvane's dither and DC filter when a ZL Limiter preset or session loads
     */
    juce::ValueTree withDefaults(const juce::ValueTree& saved, const juce::AudioProcessorValueTreeState& tree,
                                 const juce::AudioProcessor& owner) {
        auto state = saved.createCopy();
        for (auto* para : owner.getParameters()) {
            const auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(para);
            if (ranged == nullptr || tree.getParameter(ranged->getParameterID()) != ranged ||
                state.getChildWithProperty("id", ranged->getParameterID()).isValid()) {
                continue;
            }
            juce::ValueTree child("PARAM");
            child.setProperty("id", ranged->getParameterID(), nullptr);
            child.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
            state.appendChild(child, nullptr);
        }
        return state;
    }
}

PluginProcessor::PluginProcessor() :
    AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        ),
    dummy_processor_(),
    parameters_(*this, nullptr,
                juce::Identifier(zlstate::schema::kParameterState),
                altvane::limiter::params::getParameterLayout()),
    na_parameters_(dummy_processor_, nullptr,
                   juce::Identifier(zlstate::schema::kNonAutomatableState),
                   zlstate::getNAParameterLayout()),
    controller_(*this),
    engine_attach_(parameters_, controller_),
    bypass_(raw(parameters_, zlp::PBypass::kID)),
    delta_(raw(parameters_, zlp::PDelta::kID)),
    dc_filter_on_(raw(parameters_, altvane::limiter::params::PDCFilter::kID)),
    dither_bits_(raw(parameters_, altvane::limiter::params::PDitherBits::kID)),
    dither_shape_(raw(parameters_, altvane::limiter::params::PDitherShape::kID)),
    loudness_auto_reset_(raw(na_parameters_, zlstate::PLoudnessAutoReset::kID)),
    oversampling_(raw(parameters_, zlp::POversampling::kID)) {
    // oversampling changes are applied from here, on the message thread (see applyOversampling)
    startTimerHz(20);
}

PluginProcessor::~PluginProcessor() {
    stopTimer();
}

const juce::String PluginProcessor::getName() const {
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const {
    return false;
}

bool PluginProcessor::producesMidi() const {
    return false;
}

bool PluginProcessor::isMidiEffect() const {
    return false;
}

double PluginProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int PluginProcessor::getNumPrograms() {
    return 1;
}

int PluginProcessor::getCurrentProgram() {
    return 0;
}

void PluginProcessor::setCurrentProgram(int) {
}

const juce::String PluginProcessor::getProgramName(int) {
    return "Default";
}

void PluginProcessor::changeProgramName(int, const juce::String&) {
}

void PluginProcessor::prepareToPlay(const double sample_rate, const int samples_per_block) {
    sample_rate_.store(sample_rate, std::memory_order::relaxed);
    was_playing_ = false;

    const juce::PluginHostType host_type;
    update_channel_layout_per_call_ = host_type.isMaschine();

    lockEngine();
    max_block_size_ = static_cast<size_t>(std::max(1, samples_per_block));
    const auto oversampling = wantedOversampling();
    controller_.setOversamplingIndex(oversampling);
    controller_.prepare(sample_rate, max_block_size_);
    applied_oversampling_.store(oversampling, std::memory_order::relaxed);

    const auto channels = static_cast<size_t>(std::max(1, getMainBusNumInputChannels()));
    dc_filter_.prepare(sample_rate);
    dither_.prepare(sample_rate);
    loudness_meter_.prepare(sample_rate, channels, max_block_size_);

    updateChannelLayout();
    prepared_.store(true, std::memory_order::release);
    unlockEngine();
}

void PluginProcessor::releaseResources() {
    prepared_.store(false, std::memory_order::release);
    was_playing_ = false;
}

void PluginProcessor::reset() {
    // a reset that meets the message thread re-preparing the engine leaves it to that, which resets it anyway
    if (!engine_busy_.exchange(true, std::memory_order::acquire)) {
        controller_.reset();
        unlockEngine();
    }
    dc_filter_.reset();
    dither_.reset();
    was_playing_ = false;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()) {
        return true;
    }
    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()) {
        return true;
    }
    return false;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processBlockInternal(buffer, false);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processBlockInternal(buffer, true);
}

bool PluginProcessor::hasEditor() const {
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new altvane::limiter::Editor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& dest_data) {
    auto temp_tree = juce::ValueTree(zlstate::schema::kProcessorState);
    temp_tree.appendChild(parameters_.copyState(), nullptr);
    temp_tree.appendChild(na_parameters_.copyState(), nullptr);
    const std::unique_ptr<juce::XmlElement> xml(temp_tree.createXml());
    copyXmlToBinary(*xml, dest_data);
}

void PluginProcessor::setStateInformation(const void* data, const int size_in_bytes) {
    std::unique_ptr<juce::XmlElement> xml_state(getXmlFromBinary(data, size_in_bytes));
    if (xml_state == nullptr || !xml_state->hasTagName(zlstate::schema::kProcessorState)) {
        return;
    }

    const auto temp_tree = juce::ValueTree::fromXml(*xml_state);
    const auto parameter_state = temp_tree.getChildWithName(
        juce::Identifier(zlstate::schema::kParameterState));
    const auto non_automatable_state = temp_tree.getChildWithName(
        juce::Identifier(zlstate::schema::kNonAutomatableState));

    if (!parameter_state.isValid() || !non_automatable_state.isValid()) {
        return;
    }

    parameters_.replaceState(withDefaults(parameter_state, parameters_, *this));
    na_parameters_.replaceState(withDefaults(non_automatable_state, na_parameters_, dummy_processor_));
}

int PluginProcessor::wantedOversampling() const {
    return std::clamp(static_cast<int>(std::lround(oversampling_.load(std::memory_order::relaxed))), 0,
                      zlp::POversampling::kChoices.size() - 1);
}

void PluginProcessor::lockEngine() {
    // the audio thread holds it for one block at most
    while (engine_busy_.exchange(true, std::memory_order::acquire)) {
        std::this_thread::yield();
    }
}

void PluginProcessor::unlockEngine() {
    engine_busy_.store(false, std::memory_order::release);
}

// Altvane: the engine switches oversampling inside its process() and then posts a message to report the latency,
// which must not happen on the audio thread. EngineAttach keeps the parameter away from the engine; here the message
// thread's timer hands it over and re-prepares the engine, as a host's prepare does, which reports the latency from
// this thread. (Not from a parameter listener: those run under the parameter's listener lock, which a host thread
// setting the same parameter would then wait on.) The audio thread plays silence for any block that arrives
// meanwhile: switching resets the engine (and so drops a latency's worth of audio) in any case
void PluginProcessor::applyOversampling() {
    // offline, the render thread switches (see processBlockInternal)
    if (isNonRealtime() || !prepared_.load(std::memory_order::acquire) ||
        wantedOversampling() == applied_oversampling_.load(std::memory_order::relaxed)) {
        return;
    }
    lockEngine();
    if (const auto oversampling = wantedOversampling();
        prepared_.load(std::memory_order::acquire) &&
        oversampling != applied_oversampling_.load(std::memory_order::relaxed)) {
        controller_.setOversamplingIndex(oversampling);
        controller_.prepare(sample_rate_.load(std::memory_order::relaxed), max_block_size_);
        applied_oversampling_.store(oversampling, std::memory_order::relaxed);
    }
    unlockEngine();
}

void PluginProcessor::timerCallback() {
    applyOversampling();
}

void PluginProcessor::updateChannelLayout() {
    const auto* main_bus = getBus(true, 0);
    channel_layout_ = kInvalid;
    if (main_bus != nullptr) {
        if (main_bus->getCurrentLayout() == juce::AudioChannelSet::mono()) {
            channel_layout_ = kMain1;
        } else if (main_bus->getCurrentLayout() == juce::AudioChannelSet::stereo()) {
            channel_layout_ = kMain2;
        }
    }
    num_main_channels_.store(channel_layout_ == kMain1 ? 1 : (channel_layout_ == kMain2 ? 2 : 0),
                             std::memory_order::relaxed);
}

// Altvane: loudness counts while the host plays (always, without a transport) and starts over when playback starts
// (replaces upstream's value-measurement transport tracking, which fed the editor-side meters)
void PluginProcessor::updateLoudnessTransport() {
    const auto* playhead = getPlayHead();
    const auto position = playhead != nullptr
        ? playhead->getPosition()
        : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
    if (!position.hasValue()) {
        loudness_meter_.setAccumulating(true);
        return;
    }
    const auto playing = position->getIsPlaying();
    if (playing && !was_playing_ &&
        loudness_auto_reset_.load(std::memory_order::relaxed) > .5f) {
        loudness_meter_.requestReset();
    }
    was_playing_ = playing;
    loudness_meter_.setAccumulating(playing);
}

void PluginProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer, const bool bypass) {
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0) {
        return; // ignore empty blocks
    }
    // the message thread is switching the engine's oversampling: play silence rather than wait, except in an offline
    // render, which has no deadline and must not lose audio
    if (isNonRealtime()) {
        lockEngine();
    } else if (engine_busy_.exchange(true, std::memory_order::acquire)) {
        buffer.clear();
        return;
    }
    const struct Leave {
        PluginProcessor& p;
        ~Leave() { p.unlockEngine(); }
    } leave{*this};
    if (isNonRealtime()) {
        // an offline render has no deadline, and the message thread may not run between its blocks: switch here, so
        // the change lands on this block (the engine then reports the latency through its own message)
        if (const auto oversampling = wantedOversampling();
            oversampling != applied_oversampling_.load(std::memory_order::relaxed)) {
            controller_.setOversamplingIndex(oversampling);
            applied_oversampling_.store(oversampling, std::memory_order::relaxed);
        }
    }
    if (update_channel_layout_per_call_) {
        updateChannelLayout();
    }
    const auto num_main_channels = channel_layout_ == kMain1 ? 1 : (channel_layout_ == kMain2 ? 2 : 0);
    if (num_main_channels == 0) {
        buffer.clear();
        return;
    }
    updateLoudnessTransport();
    host_bypassed_.store(bypass, std::memory_order::relaxed);

    // the engine's buffers hold the prepared block size; hosts may send more, so work in pieces of at most that
    const auto total = static_cast<size_t>(buffer.getNumSamples());
    for (size_t start = 0; start < total; start += max_block_size_) {
        std::array<float*, 2> main_pointers{};
        for (int channel = 0; channel < num_main_channels; ++channel) {
            main_pointers[static_cast<size_t>(channel)] = buffer.getWritePointer(channel) + start;
        }
        processPiece(std::span<float*>{main_pointers.data(), static_cast<size_t>(num_main_channels)},
                     std::min(max_block_size_, total - start), bypass);
    }
}

void PluginProcessor::processPiece(const std::span<float*> main, const size_t num_samples, const bool bypass) {
    // the output stage only touches what the limiter does: bypass passes the input, delta the removed signal
    const auto bypassed = bypass || bypass_.load(std::memory_order::relaxed) > .5f;
    const auto processing = !bypassed && delta_.load(std::memory_order::relaxed) < .5f;

    // the DC filter while the limiter is heard, faded in and out (bypass included)
    dc_filter_.setOn(!bypassed && dc_filter_on_.load(std::memory_order::relaxed) > .5f);
    dc_filter_.process(main, num_samples);

    // when Audition turns on while the engine's delayed copy of the gained input is idle (editor closed), the engine
    // plays the limited signal until that delay has filled, one latency; keep the output silent until the
    // subtraction lines up instead of letting that through
    const auto delta_on = !bypassed && !processing;
    if (delta_on && !delta_was_on_) {
        audition_warmup_ = static_cast<size_t>(std::max(0, getLatencySamples()));
    }
    delta_was_on_ = delta_on;

    controller_.process(main, num_samples, bypass);

    if (delta_on && audition_warmup_ > 0) {
        for (auto* channel : main) {
            std::fill(channel, channel + num_samples, 0.f);
        }
        audition_warmup_ -= std::min(audition_warmup_, num_samples);
    }

    const auto bits_index = std::clamp(static_cast<int>(std::lround(dither_bits_.load(std::memory_order::relaxed))),
                                       0, static_cast<int>(altvane::limiter::params::PDitherBits::kBits.size()) - 1);
    dither_.setBits(processing ? altvane::limiter::params::PDitherBits::kBits[static_cast<size_t>(bits_index)] : 0);
    dither_.setShape(dither_shape_.load(std::memory_order::relaxed) > .5f
                         ? altvane::limiter::Dither::Shape::kShaped
                         : altvane::limiter::Dither::Shape::kFlat);
    dither_.process(main, num_samples);

    std::array<const float*, 2> measured{main[0], main.size() > 1 ? main[1] : main[0]};
    loudness_meter_.process(std::span<const float* const>{measured.data(), main.size()}, num_samples);
}

juce::AudioProcessor*JUCE_CALLTYPE

createPluginFilter() {
    return new PluginProcessor();
}
