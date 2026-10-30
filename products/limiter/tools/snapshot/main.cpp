// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.hpp"
#include "ui/editor.hpp"
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

/**
 * renders the editor to PNGs for visual review, driving the real processor with synthetic music
 *   AltvaneLimiterSnapshot <out-dir> [scale] [music.wav]
 */
namespace {
    void set(juce::AudioProcessorValueTreeState& tree, const char* id, const float plain) {
        auto* para = tree.getParameter(id);
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

    /**
     * four bars of a drum loop, bass and a pad at 120 bpm, louder in the second half
     */
    juce::AudioBuffer<float> syntheticMusic(const int length, const double rate) {
        constexpr auto pi = juce::MathConstants<double>::pi;
        juce::AudioBuffer<float> buffer(2, length);
        juce::Random random(7);
        const auto beat = 60.0 / 120.0;
        for (int i = 0; i < length; ++i) {
            const auto t = static_cast<double>(i) / rate;
            const auto in_beat = std::fmod(t, beat);
            const auto bar_pos = std::fmod(t, 4.0 * beat);
            const auto section = std::fmod(t, 16.0 * beat) < 8.0 * beat ? .7 : 1.0;
            // kick on every beat: a falling sine and a click
            const auto kick_f = 50.0 + 120.0 * std::exp(-in_beat * 40.0);
            const auto kick = std::sin(2.0 * pi * kick_f * in_beat) * std::exp(-in_beat * 9.0) +
                              .3 * std::exp(-in_beat * 400.0);
            // snare on two and four
            const auto snare_t = std::fmod(bar_pos + beat, 2.0 * beat);
            const auto snare = (random.nextDouble() * 2.0 - 1.0) * std::exp(-snare_t * 22.0) * .7 +
                               std::sin(2.0 * pi * 190.0 * snare_t) * std::exp(-snare_t * 30.0) * .3;
            // hats on the off-beats
            const auto hat_t = std::fmod(t + beat * .5, beat);
            const auto hat = (random.nextDouble() * 2.0 - 1.0) * std::exp(-hat_t * 90.0) * .18;
            // bass and a pad
            const auto root = bar_pos < 2.0 * beat ? 55.0 : 73.4;
            const auto bass = .35 * std::sin(2.0 * pi * root * t) + .12 * std::sin(2.0 * pi * root * 2.0 * t);
            double pad = 0.0;
            for (const auto f : {220.0, 277.2, 329.6, 440.0}) {
                pad += .05 * std::sin(2.0 * pi * f * t + std::sin(2.0 * pi * .3 * t));
            }
            const auto l = section * (.55 * kick + .32 * snare + hat + bass + pad * 1.1);
            const auto r = section * (.55 * kick + .30 * snare + hat * .7 + bass + pad * .9);
            buffer.setSample(0, i, static_cast<float>(.5 * l));
            buffer.setSample(1, i, static_cast<float>(.5 * r));
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
    constexpr int kBlock = 512;
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
        source = syntheticMusic(static_cast<int>(kSampleRate * 8.0), kSampleRate);
    }

    PluginProcessor p;
    // oversampling before the prepare, which applies it (afterwards it waits for the message thread's timer)
    set(p.parameters_, zlp::POversampling::kID, 2.f);
    p.setPlayConfigDetails(2, 2, kSampleRate, kBlock);
    p.prepareToPlay(kSampleRate, kBlock);
    set(p.parameters_, zlp::PInputGain::kID, 7.f);
    set(p.parameters_, zlp::POutputCeiling::kID, -1.f);

    std::unique_ptr<juce::AudioProcessorEditor> editor_base(p.createEditor());
    auto* editor = dynamic_cast<altvane::limiter::Editor*>(editor_base.get());
    if (editor == nullptr) {
        std::cerr << "editor is not an Altvane Limiter editor" << std::endl;
        return 1;
    }
    const auto default_size = juce::Point<int>{altvane::limiter::tokens::size::kDefaultWidth,
                                               altvane::limiter::tokens::size::kDefaultHeight};
    editor->setSize(default_size.x, default_size.y);
    // without a window the editor never shows, so start the analyzer by hand
    p.getController().setAnalyzerEnabled(true);

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

    run(12.0);
    writePNG(*editor, out_dir.getChildFile("01_overview.png"), scale_factor);

    auto& history = editor->getHistoryForTesting();
    const auto plot = history.getPlotArea();
    history.setHoverForTesting(juce::Point<float>{plot.getX() + plot.getWidth() * .62f, plot.getCentreY()});
    run(.05);
    writePNG(*editor, out_dir.getChildFile("02_hover.png"), scale_factor);
    history.setHoverForTesting(std::nullopt);

    editor->getCardForTesting().applyCharacter(3);
    set(p.parameters_, zlp::PInputGain::kID, 12.f);
    set(p.na_parameters_, zlstate::PLoudnessTarget::kID, 7.f);
    run(4.0);
    writePNG(*editor, out_dir.getChildFile("03_aggressive_custom_target.png"), scale_factor);

    set(p.parameters_, zlp::PDelta::kID, 1.f);
    run(3.0);
    writePNG(*editor, out_dir.getChildFile("04_audition.png"), scale_factor);
    set(p.parameters_, zlp::PDelta::kID, 0.f);

    set(p.parameters_, zlp::PBypass::kID, 1.f);
    run(1.0);
    writePNG(*editor, out_dir.getChildFile("05_bypassed.png"), scale_factor);
    set(p.parameters_, zlp::PBypass::kID, 0.f);

    editor->setControlsOpen(false);
    editor->setLoudnessOpen(false);
    set(p.na_parameters_, zlstate::PAnalyzerTimeLength::kID, 3.f);
    run(2.0);
    writePNG(*editor, out_dir.getChildFile("06_display_only.png"), scale_factor);
    editor->setControlsOpen(true);
    editor->setLoudnessOpen(true);
    set(p.na_parameters_, zlstate::PAnalyzerTimeLength::kID, 1.f);

    editor->setSize(altvane::limiter::tokens::size::kMinWidth, altvane::limiter::tokens::size::kMinHeight);
    run(1.0);
    writePNG(*editor, out_dir.getChildFile("07_small_window.png"), scale_factor);
    editor->setSize(default_size.x, default_size.y);
    run(.5);

    editor->getHelpForTesting().showFor(editor->getCardForTesting().getGainKnobForTesting());
    run(.1);
    editor->getHelpForTesting().showFor(editor->getCardForTesting().getGainKnobForTesting());
    writePNG(*editor, out_dir.getChildFile("08_help.png"), scale_factor);
    editor->getHelpForTesting().hide();

    editor->openPresetsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("09_preset_browser.png"), scale_factor);
    editor->closeSheetsForTesting();
    editor->openSettingsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("10_settings.png"), scale_factor);
    editor->closeSheetsForTesting();
    run(.3);

    editor_base.reset();
    p.releaseResources();
    return 0;
}
