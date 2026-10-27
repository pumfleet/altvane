// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.hpp"
#include "../synthetic_voice.hpp"

using namespace altvane::deesser;

namespace {
    void usage() {
        std::cout << "AltvaneDeEsserRender <in.wav | voice> <out.wav> [id=value ...] [listen=trigger|delta] [block=N]\n"
                     "  renders a file (or 20 s of a synthetic voice) through Altvane De-Esser, e.g.\n"
                     "  AltvaneDeEsserRender take.wav out.wav threshold=-30 shape=1 band_low=5000 band_high=9000\n"
                     "AltvaneDeEsserRender --bench [seconds] [block]\n"
                     "  measures the CPU of one stereo instance at 48 kHz\n";
    }

    juce::AudioBuffer<float> synthetic(const double rate, const double seconds) {
        SyntheticVoice voice(rate);
        juce::AudioBuffer<float> b(2, static_cast<int>(seconds * rate));
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const auto s = voice.next();
            b.setSample(0, i, s);
            b.setSample(1, i, s * .94f);
        }
        return b;
    }

    /**
     * process in place, in blocks; the output is shifted back by the latency
     * @return seconds spent processing
     */
    double render(PluginProcessor& p, juce::AudioBuffer<float>& audio, const double rate, const int block) {
        const auto channels = audio.getNumChannels();
        p.setPlayConfigDetails(channels, channels, rate, block);
        p.setNonRealtime(true);
        p.prepareToPlay(rate, block);
        const auto latency = p.getLatencySamples();
        const auto length = audio.getNumSamples();
        juce::AudioBuffer<float> padded(channels, length + latency);
        padded.clear();
        for (int c = 0; c < channels; ++c) {
            padded.copyFrom(c, 0, audio, c, 0, length);
        }
        juce::MidiBuffer midi;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < padded.getNumSamples(); pos += block) {
            const auto n = std::min(block, padded.getNumSamples() - pos);
            juce::AudioBuffer<float> view(padded.getArrayOfWritePointers(), channels, pos, n);
            p.processBlock(view, midi);
        }
        const auto seconds = (juce::Time::getMillisecondCounterHiRes() - start) * .001;
        for (int c = 0; c < channels; ++c) {
            audio.copyFrom(c, 0, padded, c, latency, length);
        }
        p.releaseResources();
        return seconds;
    }
}

int main(const int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) {
        usage();
        return 1;
    }
    const juce::String first = argv[1];
    if (first == "--bench") {
        const auto seconds = argc > 2 ? std::atof(argv[2]) : 60.0;
        const auto block = argc > 3 ? std::atoi(argv[3]) : 128;
        constexpr double rate = 48000.0;
        auto audio = synthetic(rate, seconds);
        for (const auto* config : {"Split, shelf", "Split, bell", "Wide"}) {
            PluginProcessor p;
            auto set = [&p](const char* id, const float v) {
                auto* para = p.parameters_.getParameter(id);
                para->setValueNotifyingHost(para->convertTo0to1(v));
            };
            set(params::kThreshold, -30.f);
            set(params::kProcessing, juce::String(config) == "Wide" ? 0.f : 1.f);
            set(params::kShape, juce::String(config) == "Split, bell" ? 1.f : 0.f);
            auto copy = audio;
            const auto spent = render(p, copy, rate, block);
            std::cout << config << ": " << juce::String(100.0 * spent / seconds, 3) << "% of one core ("
                << juce::String(seconds / spent, 0) << "x real time), block " << block << std::endl;
        }
        return 0;
    }
    if (argc < 3) {
        usage();
        return 1;
    }

    juce::AudioBuffer<float> audio;
    double rate = 48000.0;
    if (first == "voice") {
        audio = synthetic(rate, 20.0);
    } else {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(first)));
        if (reader == nullptr) {
            std::cerr << "cannot read " << first << std::endl;
            return 1;
        }
        rate = reader->sampleRate;
        audio.setSize(static_cast<int>(std::min(reader->numChannels, 2u)), static_cast<int>(reader->lengthInSamples));
        reader->read(&audio, 0, audio.getNumSamples(), 0, true, audio.getNumChannels() > 1);
    }

    PluginProcessor p;
    int block = 512;
    for (int i = 3; i < argc; ++i) {
        const juce::String arg = argv[i];
        const auto key = arg.upToFirstOccurrenceOf("=", false, false);
        const auto value = arg.fromFirstOccurrenceOf("=", false, false);
        if (key == "listen") {
            p.setListen(value == "trigger" ? dsp::Listen::kTrigger : value == "delta" ? dsp::Listen::kDelta : dsp::Listen::kOff);
        } else if (key == "block") {
            block = std::max(1, value.getIntValue());
        } else if (auto* para = p.parameters_.getParameter(key)) {
            para->setValueNotifyingHost(para->getValueForText(value));
        } else {
            std::cerr << "unknown setting " << arg << std::endl;
            return 1;
        }
    }
    p.getEngine().setFeedsActive(false);
    const auto spent = render(p, audio, rate, block);
    const auto out_file = juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]);
    out_file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
        new juce::FileOutputStream(out_file), rate, static_cast<unsigned>(audio.getNumChannels()), 24, {}, 0));
    if (writer == nullptr) {
        std::cerr << "cannot write " << out_file.getFullPathName() << std::endl;
        return 1;
    }
    writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    std::cout << "wrote " << out_file.getFullPathName() << " (" << juce::String(audio.getNumSamples() / rate / spent, 0)
        << "x real time)" << std::endl;
    return 0;
}
