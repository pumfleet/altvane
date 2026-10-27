// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "PluginProcessor.hpp"
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "ui/editor.hpp"
#include "../synthetic_voice.hpp"

using namespace altvane::deesser;

namespace {
    void set(PluginProcessor& p, const juce::String& id, const float plain) {
        if (auto* para = p.parameters_.getParameter(id)) {
            para->setValueNotifyingHost(para->convertTo0to1(plain));
        } else {
            std::cerr << "unknown parameter " << id << std::endl;
        }
    }

    void writePNG(juce::Component& c, const juce::File& file, const float scale) {
        const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat png;
        png.writeImageToStream(image, out);
        std::cout << "wrote " << file.getFullPathName() << std::endl;
    }

    juce::MouseEvent mouseAt(juce::Component& c, const juce::Point<float> p) {
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        return {source, p, {}, juce::MouseInputSource::defaultPressure, 0.f, 0.f, 0.f, 0.f, &c, &c,
                juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false};
    }
}

int main(const int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto out_dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                  : juce::File::getCurrentWorkingDirectory();
    const auto scale_factor = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 2.f;
    out_dir.createDirectory();

    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 480;
    PluginProcessor p;
    p.setPlayConfigDetails(2, 2, kSampleRate, kBlock);
    set(p, params::kThreshold, -31.f);
    set(p, params::kRange, 10.f);
    p.prepareToPlay(kSampleRate, kBlock);

    std::unique_ptr<juce::AudioProcessorEditor> editor_base(p.createEditor());
    auto* editor = dynamic_cast<Editor*>(editor_base.get());
    if (editor == nullptr) {
        std::cerr << "editor is not an Altvane De-Esser editor" << std::endl;
        return 1;
    }
    editor->setSize(1000, 640);

    SyntheticVoice voice(kSampleRate);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    double t = 0.0;
    double audio_time = 0.0;
    auto run = [&](const double seconds) {
        const auto frames = static_cast<int>(seconds * 60.0);
        for (int f = 0; f < frames; ++f) {
            t += 1.0 / 60.0;
            while (audio_time < t) {
                for (int i = 0; i < kBlock; ++i) {
                    const auto s = voice.next();
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s * .94f);
                }
                p.processBlock(buffer, midi);
                audio_time += kBlock / kSampleRate;
            }
            editor->runFrameForTesting(t);
#if JUCE_MAC
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, .002, false);
#endif
        }
    };
    // stop on a loud ess, so the picture shows the de-esser at work
    auto runToEss = [&](const double seconds) {
        run(seconds);
        const auto phase = std::fmod(audio_time, 2.5);
        run(std::fmod(1.50 - phase + 2.5, 2.5));
    };

    runToEss(5.0);
    writePNG(*editor, out_dir.getChildFile("01_overview.png"), scale_factor);

    // the finder fits a bell to the esses
    set(p, params::kShape, 1.f);
    editor->getBandViewForTesting().fitToFinder();
    runToEss(2.0);
    {
        auto& band = editor->getBandViewForTesting();
        const auto plot = band.getPlot();
        band.hoverForTesting({band.freqX(std::sqrt(p.parameters_.getRawParameterValue(params::kBandLow)->load()
                                                   * p.parameters_.getRawParameterValue(params::kBandHigh)->load())),
                              plot.getCentreY()});
    }
    run(.05);
    writePNG(*editor, out_dir.getChildFile("02_bell_fitted.png"), scale_factor);

    // learning the threshold
    set(p, params::kShape, 0.f);
    editor->getBandViewForTesting().hoverForTesting({-10.f, -10.f});
    editor->getDeckForTesting().toggleLearn();
    run(2.6);
    writePNG(*editor, out_dir.getChildFile("03_learning.png"), scale_factor);
    editor->getDeckForTesting().toggleLearn();
    run(.3);
    std::cout << "learned threshold " << p.parameters_.getRawParameterValue(params::kThreshold)->load() << std::endl;
    writePNG(*editor, out_dir.getChildFile("04_learned.png"), scale_factor);

    // wide band, 8 seconds, hovering the history
    set(p, params::kProcessing, 0.f);
    editor->getHistoryForTesting().setSeconds(8.0);
    runToEss(4.0);
    {
        auto& history = editor->getHistoryForTesting();
        history.mouseMove(mouseAt(history, {static_cast<float>(history.getWidth()) - 120.f, 120.f}));
    }
    run(.05);
    writePNG(*editor, out_dir.getChildFile("05_wide_8s_hover.png"), scale_factor);
    editor->getHistoryForTesting().mouseExit(mouseAt(editor->getHistoryForTesting(), {-1.f, -1.f}));
    editor->getHistoryForTesting().setSeconds(4.0);
    set(p, params::kProcessing, 1.f);

    // listening to what is taken
    p.setListen(dsp::Listen::kDelta);
    runToEss(1.5);
    writePNG(*editor, out_dir.getChildFile("06_delta.png"), scale_factor);
    p.setListen(dsp::Listen::kOff);

    // the smallest window
    editor->setSize(800, 520);
    runToEss(1.0);
    writePNG(*editor, out_dir.getChildFile("07_small_window.png"), scale_factor);
    editor->setSize(1000, 640);

    // overlays
    editor->openPresetsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("08_preset_browser.png"), scale_factor);
    editor->closeSheetsForTesting();
    editor->openSettingsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("09_settings.png"), scale_factor);
    editor->closeSheetsForTesting();
    run(.3);

    editor_base.reset();
    p.releaseResources();
    return 0;
}
