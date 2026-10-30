// Copyright (C) 2026 - zsliu98
// This file is part of ZLCompressor
//
// ZLCompressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLCompressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLCompressor. If not, see <https://www.gnu.org/licenses/>.

#include "PluginProcessor.hpp"
#include "ui/editor.hpp"

namespace {
    juce::ValueTree copyWithType(const juce::ValueTree& source, const juce::Identifier& type) {
        if (!source.isValid()) {
            return {};
        }

        juce::ValueTree result(type);
        result.copyPropertiesAndChildrenFrom(source, nullptr);
        return result;
    }

    juce::ValueTree getChildWithLegacyFallback(const juce::ValueTree& parent,
                                               const juce::Identifier& type,
                                               const juce::Identifier& legacy_type) {
        const auto child = parent.getChildWithName(type);
        return child.isValid() ? child : parent.getChildWithName(legacy_type);
    }
}

//==============================================================================
PluginProcessor::PluginProcessor() :
    AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withInput("Aux", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        ),
    dummy_processor_(),
    parameters_(*this, nullptr,
                juce::Identifier(zlstate::schema::kParameterState),
                zlp::getParameterLayout()),
    na_parameters_(dummy_processor_, nullptr,
                   juce::Identifier(zlstate::schema::kNonAutomatableState),
                   zlstate::getNAParameterLayout()),
    compress_controller_(*this),
    compress_attach_(*this, parameters_, compress_controller_),
    equalize_controller_(),
    equalize_attach_(*this, parameters_, equalize_controller_),
    ext_side_(*parameters_.getRawParameterValue(zlp::PExtSide::kID)),
    side_out_(*parameters_.getRawParameterValue(zlp::PSideOut::kID)) {
    addListener(this);
}

PluginProcessor::~PluginProcessor() {
    removeListener(this);
}

void PluginProcessor::audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) {
    // Altvane: the engine reports its latency from the message thread; keep a copy the audio thread may read
    if (details.latencyChanged) {
        reported_latency_.store(getLatencySamples(), std::memory_order::relaxed);
    }
}

//==============================================================================
const juce::String PluginProcessor::getName() const {
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const {
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::producesMidi() const {
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PluginProcessor::isMidiEffect() const {
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
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

//==============================================================================
void PluginProcessor::prepareToPlay(const double sample_rate, const int samples_per_block) {
    // prepare to play
    max_block_size_ = static_cast<size_t>(std::max(1, samples_per_block));
    const auto block = static_cast<int>(max_block_size_);
    float_buffer_.setSize(4, block);
    float_buffer_.clear();
    main_pointers_[0] = float_buffer_.getWritePointer(0);
    main_pointers_[1] = float_buffer_.getWritePointer(1);
    float_side_pointers_[0] = float_buffer_.getWritePointer(2);
    float_side_pointers_[1] = float_buffer_.getWritePointer(3);
    double_buffer_.setSize(2, block);
    double_side_pointers_[0] = double_buffer_.getWritePointer(0);
    double_side_pointers_[1] = double_buffer_.getWritePointer(1);
    double_buffer_.clear();
    compress_controller_.prepare(sample_rate, max_block_size_);
    equalize_controller_.prepare(sample_rate, max_block_size_);
    sample_rate_.store(sample_rate, std::memory_order::relaxed);
    // Altvane: the input kept for host bypass, long enough for the longest latency (20 ms of lookahead and the
    // oversamplers' filters, at most 74 samples) and a block on top
    const auto capacity = static_cast<size_t>(std::ceil(sample_rate * .02)) + 128 + max_block_size_ + 1;
    for (auto& dry : dry_buffers_) {
        dry.assign(capacity, 0.0);
    }
    dry_pos_ = 0;
    reported_latency_.store(getLatencySamples(), std::memory_order::relaxed);
    // determine current channel layout
    const auto* main_bus = getBus(true, 0);
    const auto* aux_bus = getBus(true, 1);
    channel_layout_ = ChannelLayout::kInvalid;
    if (main_bus == nullptr) {
        return;
    }
    if (main_bus->getCurrentLayout() == juce::AudioChannelSet::mono()) {
        if (aux_bus == nullptr || !aux_bus->isEnabled()) {
            channel_layout_ = ChannelLayout::kMain1Aux0;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::mono()) {
            channel_layout_ = ChannelLayout::kMain1Aux1;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::stereo()) {
            channel_layout_ = ChannelLayout::kMain1Aux2;
        }
    } else if (main_bus->getCurrentLayout() == juce::AudioChannelSet::stereo()) {
        if (aux_bus == nullptr || !aux_bus->isEnabled()) {
            channel_layout_ = ChannelLayout::kMain2Aux0;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::mono()) {
            channel_layout_ = ChannelLayout::kMain2Aux1;
        } else if (aux_bus->getCurrentLayout() == juce::AudioChannelSet::stereo()) {
            channel_layout_ = ChannelLayout::kMain2Aux2;
        }
    }
}

void PluginProcessor::releaseResources() {
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
        (layouts.getChannelSet(true, 1).isDisabled() ||
            layouts.getChannelSet(true, 1) == juce::AudioChannelSet::mono() ||
            layouts.getChannelSet(true, 1) == juce::AudioChannelSet::stereo())) {
        return true;
    }
    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono() &&
        (layouts.getChannelSet(true, 1).isDisabled() ||
            layouts.getChannelSet(true, 1) == juce::AudioChannelSet::mono() ||
            layouts.getChannelSet(true, 1) == juce::AudioChannelSet::stereo())) {
        return true;
    }
    return false;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processBlockInternal<false>(buffer);
}

void PluginProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) {
    processBlockInternal<false>(buffer);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    processBlockInternal<true>(buffer);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) {
    processBlockInternal<true>(buffer);
}

template <bool IsBypassed, typename T>
void PluginProcessor::processBlockInternal(juce::AudioBuffer<T>& buffer) {
    juce::ScopedNoDenormals no_denormals;
    if (buffer.getNumSamples() == 0 || channel_layout_ == ChannelLayout::kInvalid) {
        return; // ignore empty blocks
    }
    // Altvane: the engine's buffers hold the prepared block size; hosts may send more, so work in pieces of at
    // most that (upstream overran its buffers)
    const auto total = static_cast<size_t>(buffer.getNumSamples());
    const auto num_channels = std::min(buffer.getNumChannels(), 4);
    for (size_t start = 0; start < total; start += max_block_size_) {
        const auto num_samples = std::min(max_block_size_, total - start);
        std::array<T*, 4> io{};
        for (int chan = 0; chan < num_channels; ++chan) {
            io[static_cast<size_t>(chan)] = buffer.getWritePointer(chan) + start;
        }
        // Altvane: host bypass plays the input delayed by the reported latency, whatever the engine, delta, the
        // side-chain listen or a solo would play (upstream played them in bypass too)
        const auto dry_start = pushDry(io, num_samples);
        processPiece<IsBypassed>(io, num_samples);
        if constexpr (IsBypassed) {
            playDry(io, num_samples, dry_start);
        }
    }
}

template <typename T>
size_t PluginProcessor::pushDry(const std::array<T*, 4>& io, const size_t num_samples) {
    const auto start = dry_pos_;
    const auto capacity = dry_buffers_[0].size();
    for (size_t chan = 0; chan < getNumMainChannels(); ++chan) {
        auto& dry = dry_buffers_[chan];
        auto pos = start;
        for (size_t i = 0; i < num_samples; ++i) {
            dry[pos] = static_cast<double>(io[chan][i]);
            pos = pos + 1 == capacity ? 0 : pos + 1;
        }
    }
    dry_pos_ = (start + num_samples) % capacity;
    return start;
}

template <typename T>
void PluginProcessor::playDry(const std::array<T*, 4>& io, const size_t num_samples, const size_t start) const {
    const auto capacity = dry_buffers_[0].size();
    // the buffer holds the longest latency and a block on top
    const auto latency = static_cast<size_t>(std::max(0, reported_latency_.load(std::memory_order::relaxed)));
    const auto delay = std::min(latency, capacity - max_block_size_ - 1);
    for (size_t chan = 0; chan < getNumMainChannels(); ++chan) {
        const auto& dry = dry_buffers_[chan];
        auto pos = (start + capacity - delay) % capacity;
        for (size_t i = 0; i < num_samples; ++i) {
            io[chan][i] = static_cast<T>(dry[pos]);
            pos = pos + 1 == capacity ? 0 : pos + 1;
        }
    }
}

template <bool IsBypassed>
void PluginProcessor::processPiece(const std::array<float*, 4>& io, const size_t buffer_size) {
    const auto c_ext_side = ext_side_.load(std::memory_order::relaxed) > .5f;
    const auto c_side_out = side_out_.load(std::memory_order::relaxed) > .5f;
    auto main_pointers = main_pointers_;
    auto double_side_pointers = double_side_pointers_;
    auto float_side_pointers = float_side_pointers_;

    switch (channel_layout_) {
    case ChannelLayout::kMain1Aux0: {
        main_pointers[0] = io[0];
        zldsp::vector::copy(main_pointers[1], main_pointers[0], buffer_size);

        zldsp::vector::copy<double, float>(double_side_pointers, main_pointers, buffer_size);
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy(main_pointers[0], equalize_controller_.getSoloPointers()[0], buffer_size);
        } else if (c_side_out) {
            zldsp::vector::copy(main_pointers[0], double_side_pointers[0], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain1Aux1: {
        main_pointers[0] = io[0];
        zldsp::vector::copy(main_pointers[1], main_pointers[0], buffer_size);

        if (c_ext_side) {
            zldsp::vector::copy(double_side_pointers[0], io[1], buffer_size);
            zldsp::vector::copy(double_side_pointers[1], double_side_pointers[0], buffer_size);
        } else {
            zldsp::vector::copy<double, float>(double_side_pointers, main_pointers, buffer_size);
        }
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy(main_pointers[0], equalize_controller_.getSoloPointers()[0], buffer_size);
        } else if (c_side_out) {
            zldsp::vector::copy(main_pointers[0], double_side_pointers[0], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain1Aux2: {
        main_pointers[0] = io[0];
        zldsp::vector::copy(main_pointers[1], main_pointers[0], buffer_size);

        if (c_ext_side) {
            zldsp::vector::copy(double_side_pointers[0], io[1], buffer_size);
            zldsp::vector::copy(double_side_pointers[1], io[2], buffer_size);
        } else {
            zldsp::vector::copy<double, float>(double_side_pointers, main_pointers, buffer_size);
        }
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::splitter::InplaceMSSplitter<double>::split(equalize_controller_.getSoloPointers()[0],
                                                              equalize_controller_.getSoloPointers()[1], buffer_size);
            zldsp::vector::copy(main_pointers[0], equalize_controller_.getSoloPointers()[0], buffer_size);
        } else if (c_side_out) {
            // Altvane: the mono output hears the mid of the side chain (upstream read the solo buffers, which are
            // only filled while a band is soloed)
            zldsp::splitter::InplaceMSSplitter<double>::split(double_side_pointers[0], double_side_pointers[1],
                                                              buffer_size);
            zldsp::vector::copy(main_pointers[0], double_side_pointers[0], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain2Aux0: {
        main_pointers[0] = io[0];
        main_pointers[1] = io[1];

        zldsp::vector::copy<double, float>(double_side_pointers, main_pointers, buffer_size);
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy<float, double>(main_pointers, equalize_controller_.getSoloPointers(), buffer_size);
        } else if (c_side_out) {
            zldsp::vector::copy<float, double>(main_pointers, double_side_pointers, buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain2Aux1: {
        main_pointers[0] = io[0];
        main_pointers[1] = io[1];

        if (c_ext_side) {
            zldsp::vector::copy(double_side_pointers[0], io[2], buffer_size);
            zldsp::vector::copy(double_side_pointers[1], double_side_pointers[0], buffer_size);
        } else {
            zldsp::vector::copy<double, float>(double_side_pointers, main_pointers, buffer_size);
        }
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy<float, double>(main_pointers, equalize_controller_.getSoloPointers(), buffer_size);
        } else if (c_side_out) {
            zldsp::vector::copy<float, double>(main_pointers, double_side_pointers, buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain2Aux2: {
        main_pointers[0] = io[0];
        main_pointers[1] = io[1];

        if (c_ext_side) {
            zldsp::vector::copy(double_side_pointers[0], io[2], buffer_size);
            zldsp::vector::copy(double_side_pointers[1], io[3], buffer_size);
        } else {
            zldsp::vector::copy<double, float>(double_side_pointers, main_pointers, buffer_size);
        }
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy<float, double>(main_pointers, equalize_controller_.getSoloPointers(), buffer_size);
        } else if (c_side_out) {
            zldsp::vector::copy<float, double>(main_pointers, double_side_pointers, buffer_size);
        }
        break;
    }
    case ChannelLayout::kInvalid: {
    }
    }
}

template <bool IsBypassed>
void PluginProcessor::processPiece(const std::array<double*, 4>& io, const size_t buffer_size) {
    const auto c_ext_side = ext_side_.load(std::memory_order::relaxed) > .5f;
    const auto c_side_out = side_out_.load(std::memory_order::relaxed) > .5f;
    auto main_pointers = main_pointers_;
    auto double_side_pointers = double_side_pointers_;
    auto float_side_pointers = float_side_pointers_;

    switch (channel_layout_) {
    case ChannelLayout::kMain1Aux0: {
        zldsp::vector::copy(main_pointers[0], io[0], buffer_size);
        zldsp::vector::copy(main_pointers[1], main_pointers[0], buffer_size);

        double_side_pointers[0] = io[0];
        zldsp::vector::copy(double_side_pointers[1], double_side_pointers[0], buffer_size);
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        // Altvane: honour the host bypass here too (upstream passed false)
        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy(double_side_pointers[0], equalize_controller_.getSoloPointers()[0], buffer_size);
        } else if (c_side_out) {
        } else {
            zldsp::vector::copy(double_side_pointers[0], main_pointers[0], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain1Aux1: {
        zldsp::vector::copy(main_pointers[0], io[0], buffer_size);
        zldsp::vector::copy(main_pointers[1], main_pointers[0], buffer_size);

        double_side_pointers[0] = c_ext_side ? io[1] : io[0];
        zldsp::vector::copy(double_side_pointers[1], double_side_pointers[0], buffer_size);
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy(io[0], equalize_controller_.getSoloPointers()[0], buffer_size);
        } else if (c_side_out) {
            if (c_ext_side) {
                zldsp::vector::copy(io[0], double_side_pointers[0], buffer_size);
            }
        } else {
            zldsp::vector::copy(io[0], main_pointers[0], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain1Aux2: {
        zldsp::vector::copy(main_pointers[0], io[0], buffer_size);
        zldsp::vector::copy(main_pointers[1], main_pointers[0], buffer_size);

        if (c_ext_side) {
            double_side_pointers[0] = io[1];
            double_side_pointers[1] = io[2];
        } else {
            double_side_pointers[0] = io[0];
            zldsp::vector::copy(double_side_pointers[1], double_side_pointers[0], buffer_size);
        }
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::splitter::InplaceMSSplitter<double>::split(equalize_controller_.getSoloPointers()[0],
                                                              equalize_controller_.getSoloPointers()[1], buffer_size);
            zldsp::vector::copy(io[0],
                                equalize_controller_.getSoloPointers()[0], buffer_size);
        } else if (c_side_out) {
            if (c_ext_side) {
                // Altvane: the mono output hears the mid of the side chain (upstream read the solo buffers, which
                // are only filled while a band is soloed)
                zldsp::splitter::InplaceMSSplitter<double>::split(double_side_pointers[0], double_side_pointers[1],
                                                                  buffer_size);
                zldsp::vector::copy(io[0], double_side_pointers[0], buffer_size);
            }
        } else {
            zldsp::vector::copy(io[0], main_pointers[0], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain2Aux0: {
        zldsp::vector::copy(main_pointers[0], io[0], buffer_size);
        zldsp::vector::copy(main_pointers[1], io[1], buffer_size);

        double_side_pointers[0] = io[0];
        double_side_pointers[1] = io[1];
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy(io[0], equalize_controller_.getSoloPointers()[0], buffer_size);
            zldsp::vector::copy(io[1], equalize_controller_.getSoloPointers()[1], buffer_size);
        } else if (c_side_out) {
        } else {
            zldsp::vector::copy(io[0], main_pointers[0], buffer_size);
            zldsp::vector::copy(io[1], main_pointers[1], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain2Aux1: {
        zldsp::vector::copy(main_pointers[0], io[0], buffer_size);
        zldsp::vector::copy(main_pointers[1], io[1], buffer_size);

        if (c_ext_side) {
            double_side_pointers[0] = io[2];
            zldsp::vector::copy(double_side_pointers[1], double_side_pointers[0], buffer_size);
        } else {
            double_side_pointers[0] = io[0];
            double_side_pointers[1] = io[1];
        }
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy(io[0], equalize_controller_.getSoloPointers()[0], buffer_size);
            zldsp::vector::copy(io[1], equalize_controller_.getSoloPointers()[0], buffer_size);
        } else if (c_side_out) {
            if (c_ext_side) {
                zldsp::vector::copy(io[0], double_side_pointers[0], buffer_size);
                zldsp::vector::copy(io[1], double_side_pointers[0], buffer_size);
            }
        } else {
            zldsp::vector::copy(io[0], main_pointers[0], buffer_size);
            zldsp::vector::copy(io[1], main_pointers[1], buffer_size);
        }
        break;
    }
    case ChannelLayout::kMain2Aux2: {
        zldsp::vector::copy(main_pointers[0], io[0], buffer_size);
        zldsp::vector::copy(main_pointers[1], io[1], buffer_size);

        if (c_ext_side) {
            double_side_pointers[0] = io[2];
            double_side_pointers[1] = io[3];
        } else {
            double_side_pointers[0] = io[0];
            double_side_pointers[1] = io[1];
        }
        equalize_controller_.process(double_side_pointers, buffer_size);
        zldsp::vector::copy<float, double>(float_side_pointers, double_side_pointers, buffer_size);

        compress_controller_.process(main_pointers, float_side_pointers, buffer_size, IsBypassed);

        if (equalize_controller_.getSoloOn()) {
            zldsp::vector::copy(io[0], equalize_controller_.getSoloPointers()[0], buffer_size);
            zldsp::vector::copy(io[1], equalize_controller_.getSoloPointers()[1], buffer_size);
        } else if (c_side_out) {
            if (c_ext_side) {
                zldsp::vector::copy(io[0], double_side_pointers[0], buffer_size);
                zldsp::vector::copy(io[1], double_side_pointers[1], buffer_size);
            }
        } else {
            zldsp::vector::copy(io[0], main_pointers[0], buffer_size);
            zldsp::vector::copy(io[1], main_pointers[1], buffer_size);
        }
        break;
    }
    case ChannelLayout::kInvalid: {
    }
    }
}

bool PluginProcessor::hasEditor() const {
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new altvane::compressor::Editor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& dest_data) {
    auto temp_tree = juce::ValueTree(zlstate::schema::kProcessorState);
    temp_tree.appendChild(parameters_.copyState(), nullptr);
    temp_tree.appendChild(na_parameters_.copyState(), nullptr);
    const std::unique_ptr<juce::XmlElement> xml(temp_tree.createXml());
    copyXmlToBinary(*xml, dest_data);
}

void PluginProcessor::setStateInformation(const void* data, int size_in_bytes) {
    std::unique_ptr<juce::XmlElement> xml_state(getXmlFromBinary(data, size_in_bytes));
    if (xml_state == nullptr ||
        (!xml_state->hasTagName(zlstate::schema::kProcessorState) &&
            !xml_state->hasTagName(zlstate::schema::legacy::kProcessorState))) {
        return;
    }

    const auto temp_tree = juce::ValueTree::fromXml(*xml_state);
    const auto parameter_state = getChildWithLegacyFallback(
        temp_tree,
        juce::Identifier(zlstate::schema::kParameterState),
        juce::Identifier(zlstate::schema::legacy::kParameterState));
    const auto non_automatable_state = getChildWithLegacyFallback(
        temp_tree,
        juce::Identifier(zlstate::schema::kNonAutomatableState),
        juce::Identifier(zlstate::schema::legacy::kNonAutomatableState));
    if (!parameter_state.isValid() || !non_automatable_state.isValid()) {
        return;
    }

    parameters_.replaceState(copyWithType(parameter_state,
                                          juce::Identifier(zlstate::schema::kParameterState)));
    na_parameters_.replaceState(copyWithType(non_automatable_state,
                                             juce::Identifier(zlstate::schema::kNonAutomatableState)));
}

juce::AudioProcessor* JUCE_CALLTYPE

createPluginFilter() {
    return new PluginProcessor();
}
