// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.

// Offline renderer: runs a WAV file through the plug-in, latency-compensated, and writes the mix and each stem.
//
//   AltvaneDialogueRender <in.wav> <out-dir> [--quality eco|high] [--stereo linked|stereo] [--block N]
//                         [--voice dB] [--ambience dB] [--reverb dB] [--realtime]
//
// --realtime processes through the worker thread at real-time pace instead of waiting for it (a soak test of the
// real-time path; late hops come out unprocessed and are counted).

#include <chrono>
#include <thread>
#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.hpp"
#include "dsp/kernels.hpp"

using altvane::dialogue::PluginProcessor;
namespace params = altvane::dialogue::params;
namespace dsp = altvane::dialogue::dsp;

namespace {
    void setParam(PluginProcessor& p, const char* id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    juce::AudioBuffer<float> render(PluginProcessor& p, const juce::AudioBuffer<float>& input, const double rate,
                                    const int block, const bool realtime, double& seconds) {
        const auto channels = input.getNumChannels();
        p.setPlayConfigDetails(channels, channels, rate, block);
        p.setNonRealtime(!realtime);
        p.prepareToPlay(rate, block);
        const auto latency = p.getLatencySamples();
        const auto total = input.getNumSamples() + latency;
        juce::AudioBuffer<float> out(channels, total);
        juce::AudioBuffer<float> io(channels, block);
        juce::MidiBuffer midi;
        const auto start = std::chrono::steady_clock::now();
        for (int pos = 0; pos < total; pos += block) {
            const auto n = std::min(block, total - pos);
            io.setSize(channels, n, false, false, true);
            for (int c = 0; c < channels; ++c) {
                const auto available = std::clamp(input.getNumSamples() - pos, 0, n);
                io.clear(c, 0, n);
                if (available > 0) {
                    io.copyFrom(c, 0, input, c, pos, available);
                }
            }
            p.processBlock(io, midi);
            for (int c = 0; c < channels; ++c) {
                out.copyFrom(c, pos, io, c, 0, n);
            }
            if (realtime) {
                // pace the calls like an audio device would
                std::this_thread::sleep_until(start + std::chrono::duration<double>(static_cast<double>(pos + n) / rate));
            }
        }
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        p.releaseResources();
        juce::AudioBuffer<float> aligned(channels, input.getNumSamples());
        for (int c = 0; c < channels; ++c) {
            aligned.copyFrom(c, 0, out, c, latency, input.getNumSamples());
        }
        return aligned;
    }

    bool write(const juce::File& file, const juce::AudioBuffer<float>& buffer, const double rate) {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream().release());
        if (stream == nullptr) {
            return false;
        }
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(rate)
                                                                               .withNumChannels(buffer.getNumChannels())
                                                                               .withBitsPerSample(24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }
}

int main(int argc, char* argv[]) {
    const juce::ScopedJuceInitialiser_GUI juce_init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) {
        args.add(argv[i]);
    }
    if (args.size() < 2) {
        std::cerr << "usage: AltvaneDialogueRender <in.wav> <out-dir> [--quality eco|high] [--stereo linked|stereo]"
                     " [--block N] [--voice dB] [--ambience dB] [--reverb dB] [--realtime] [--no-blas]\n";
        return 1;
    }
    auto option = [&](const juce::String& name, const juce::String& fallback) {
        const auto i = args.indexOf(name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };
    const juce::File in_file = juce::File::getCurrentWorkingDirectory().getChildFile(args[0]);
    const juce::File out_dir = juce::File::getCurrentWorkingDirectory().getChildFile(args[1]);
    out_dir.createDirectory();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(in_file));
    if (reader == nullptr) {
        std::cerr << "cannot read " << in_file.getFullPathName() << "\n";
        return 1;
    }
    const auto channels = std::min(static_cast<int>(reader->numChannels), 2);
    juce::AudioBuffer<float> input(channels, static_cast<int>(reader->lengthInSamples));
    reader->read(&input, 0, input.getNumSamples(), 0, true, channels > 1);
    const auto rate = reader->sampleRate;
    const auto block = option("--block", "512").getIntValue();
    const auto realtime = args.contains("--realtime");
    // the network's matrix products without Accelerate, to time what it brings
    dsp::kernels::setBlasEnabled(!args.contains("--no-blas"));

    PluginProcessor p;
    setParam(p, params::kQuality, option("--quality", "high") == "eco" ? 0.f : 1.f);
    setParam(p, params::kStereo, option("--stereo", "linked") == "stereo" ? 1.f : 0.f);
    setParam(p, params::kVoice, option("--voice", "0").getFloatValue());
    setParam(p, params::kAmbience, option("--ambience", "-60").getFloatValue());
    setParam(p, params::kReverb, option("--reverb", "-60").getFloatValue());

    const auto audio_seconds = static_cast<double>(input.getNumSamples()) / rate;
    struct Pass {
        const char* name;
        dsp::Listen listen;
    };
    for (const auto& pass : {Pass{"mix", dsp::Listen::kMix}, Pass{"voice", dsp::Listen::kVoice},
                             Pass{"ambience", dsp::Listen::kAmbience}, Pass{"reverb", dsp::Listen::kReverb}}) {
        p.setListen(pass.listen);
        double seconds = 0.0;
        const auto out = render(p, input, rate, block, realtime, seconds);
        const auto file = out_dir.getChildFile(juce::String(pass.name) + ".wav");
        if (!write(file, out, rate)) {
            std::cerr << "cannot write " << file.getFullPathName() << "\n";
            return 1;
        }
        std::cout << pass.name << ": " << juce::String(seconds, 2) << " s for " << juce::String(audio_seconds, 2)
                  << " s of audio (" << juce::String(100.0 * seconds / audio_seconds, 1) << "% of real time)";
        if (realtime) {
            std::cout << ", " << p.getEngine().getUnderruns() << " late samples";
        }
        std::cout << ", latency " << p.getLatencySamples() << " samples\n";
        if (realtime) {
            break;
        }
    }
    return 0;
}
