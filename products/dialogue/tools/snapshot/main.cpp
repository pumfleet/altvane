// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.

// Renders the Altvane Dialogue editor headlessly with the real processor running, and writes PNG snapshots for
// visual review. Give it a speech recording (48 kHz WAV) for a realistic picture; without one it uses a synthetic
// voice-like signal over noise.
//
// usage: AltvaneDialogueSnapshot <output-dir> [scale] [speech.wav]

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.hpp"
#include "ui/editor.hpp"
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

using altvane::dialogue::PluginProcessor;
namespace params = altvane::dialogue::params;
namespace dsp = altvane::dialogue::dsp;

namespace {
    void set(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    void writePNG(juce::Component& c, const juce::File& file, const float scale) {
        const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat png;
        png.writeImageToStream(image, out);
        std::cout << "wrote " << file.getFullPathName() << std::endl;
    }

    juce::AudioBuffer<float> syntheticVoice(const int length, const double rate) {
        juce::AudioBuffer<float> buffer(2, length);
        juce::Random random(3);
        double phase = 0.0;
        float rumble = 0.f;
        for (int i = 0; i < length; ++i) {
            const auto t = static_cast<double>(i) / rate;
            const auto syllable = std::fmod(t, .3) < .2 ? std::sin(juce::MathConstants<double>::pi * std::fmod(t, .3) / .2) : 0.0;
            phase += (130.0 + 25.0 * std::sin(2.0 * juce::MathConstants<double>::pi * .9 * t)) / rate;
            double voice = 0.0;
            for (int h = 1; h < 30; ++h) {
                const auto formant = std::exp(-std::pow((h * 130.0 - 700.0) / 500.0, 2.0)) + .5 * std::exp(-std::pow((h * 130.0 - 2400.0) / 700.0, 2.0));
                voice += std::sin(2.0 * juce::MathConstants<double>::pi * phase * h) * (.2 + formant) / h;
            }
            rumble = .995f * rumble + .05f * (random.nextFloat() * 2.f - 1.f);
            const auto noise = .02f * (random.nextFloat() * 2.f - 1.f) + .2f * rumble;
            const auto x = static_cast<float>(.25 * syllable * voice) + noise;
            buffer.setSample(0, i, x);
            buffer.setSample(1, i, x * .95f);
        }
        return buffer;
    }
}

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto out_dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                  : juce::File::getCurrentWorkingDirectory();
    const auto scale_factor = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 2.f;
    out_dir.createDirectory();

    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 480;
    juce::AudioBuffer<float> source;
    if (argc > 3) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(argv[3])));
        if (reader == nullptr) {
            std::cerr << "cannot read " << argv[3] << std::endl;
            return 1;
        }
        source.setSize(2, static_cast<int>(reader->lengthInSamples));
        reader->read(&source, 0, source.getNumSamples(), 0, true, true);
    } else {
        source = syntheticVoice(static_cast<int>(kSampleRate * 6.0), kSampleRate);
    }

    PluginProcessor p;
    p.setPlayConfigDetails(2, 2, kSampleRate, kBlock);
    // offline, so the model never falls behind and every frame of the picture is processed
    p.setNonRealtime(true);
    p.prepareToPlay(kSampleRate, kBlock);
    set(p, params::kVoice, 0.f);
    set(p, params::kAmbience, -20.f);
    set(p, params::kReverb, -10.f);

    std::unique_ptr<juce::AudioProcessorEditor> editor_base(p.createEditor());
    auto* editor = dynamic_cast<altvane::dialogue::Editor*>(editor_base.get());
    if (editor == nullptr) {
        std::cerr << "editor is not an Altvane Dialogue editor" << std::endl;
        return 1;
    }
    editor->setSize(900, 580);

    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    double t = 0.0, audio_time = 0.0;
    int pos = 0;
    auto run = [&](const double seconds) {
        const auto frames = static_cast<int>(seconds * 60.0);
        for (int f = 0; f < frames; ++f) {
            t += 1.0 / 60.0;
            while (audio_time < t) {
                for (int c = 0; c < 2; ++c) {
                    for (int i = 0; i < kBlock; ++i) {
                        buffer.setSample(c, i, source.getSample(c, (pos + i) % source.getNumSamples()));
                    }
                }
                pos += kBlock;
                p.processBlock(buffer, midi);
                audio_time += kBlock / kSampleRate;
            }
            editor->runFrameForTesting(t);
#if JUCE_MAC
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, .002, false);
#endif
        }
    };

    run(2.4);
    writePNG(*editor, out_dir.getChildFile("01_overview.png"), scale_factor);

    editor->getVisualiserForTesting().setFocus(dsp::kVoice);
    set(p, params::sensitivityID(dsp::kVoice, 1), .35f);
    set(p, params::sensitivityID(dsp::kVoice, 4), .55f);
    set(p, params::sensitivityID(dsp::kVoice, 5), .3f);
    run(.6);
    writePNG(*editor, out_dir.getChildFile("02_voice_curve.png"), scale_factor);
    editor->getVisualiserForTesting().setFocus(-1);

    p.setListen(dsp::Listen::kVoice);
    run(.8);
    writePNG(*editor, out_dir.getChildFile("03_solo_voice.png"), scale_factor);

    p.setListen(dsp::Listen::kRemoved);
    run(.8);
    writePNG(*editor, out_dir.getChildFile("04_removed.png"), scale_factor);
    p.setListen(dsp::Listen::kMix);

    set(p, params::kAmbience, params::kLevelOff);
    set(p, params::kReverb, params::kLevelOff);
    set(p, params::kQuality, 0.f);
    run(.8);
    writePNG(*editor, out_dir.getChildFile("05_clean_eco.png"), scale_factor);

    editor->setSize(680, 460);
    run(.5);
    writePNG(*editor, out_dir.getChildFile("06_small_window.png"), scale_factor);
    editor->setSize(900, 580);

    editor->openPresetsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("07_preset_browser.png"), scale_factor);
    editor->closeSheetsForTesting();
    editor->openSettingsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("08_settings.png"), scale_factor);
    editor->closeSheetsForTesting();
    run(.3);

    editor_base.reset();
    p.releaseResources();
    return 0;
}
