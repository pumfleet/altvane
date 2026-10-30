// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.

// Renders the Altvane Compressor editor headlessly with the real processor running on a synthetic drum loop,
// and writes PNG snapshots of the states in docs/design/mock.html for visual review.
//
// usage: AltvaneCompressorSnapshot <output-dir> [scale]

#include "PluginProcessor.hpp"
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "ui/editor.hpp"

namespace {
    /**
     * a drum loop at 120 bpm: kick on the beats, snare on 2 and 4, eighth-note hats, a little room noise
     */
    class DrumLoop {
    public:
        explicit DrumLoop(const double sample_rate) : sample_rate_(sample_rate) {
        }

        float next() {
            const auto beat = 60.0 / 120.0;
            const auto t = static_cast<double>(n_++) / sample_rate_;
            const auto in_beat = std::fmod(t, beat);
            const auto beat_index = static_cast<int>(t / beat) % 4;
            const auto in_eighth = std::fmod(t, beat * .5);

            // kick: a falling sine with a fast decay
            const auto kick_f = 50.0 + 90.0 * std::exp(-in_beat * 30.0);
            kick_phase_ += kick_f / sample_rate_;
            const auto kick = std::sin(kick_phase_ * juce::MathConstants<double>::twoPi) * std::exp(-in_beat * 9.0);
            // snare: noise and a tone on 2 and 4
            const auto noise = random_.nextFloat() * 2.f - 1.f;
            const auto snare_on = beat_index == 1 || beat_index == 3;
            const auto snare = snare_on ? (noise * .7 + std::sin(t * 190.0 * juce::MathConstants<double>::twoPi) * .4)
                                          * std::exp(-in_beat * 16.0)
                                        : 0.0;
            // hats: short bright noise
            hp_ = .6f * (hp_ + noise - last_noise_);
            last_noise_ = noise;
            const auto hat = hp_ * std::exp(-in_eighth * 60.0) * .25;
            const auto room = noise * .004;
            return static_cast<float>((kick * .55 + snare * .45 + hat + room) * .8);
        }

    private:
        double sample_rate_;
        int64_t n_{0};
        double kick_phase_{0.0};
        float hp_{0.f}, last_noise_{0.f};
        juce::Random random_{42};
    };

    void set(PluginProcessor& p, const juce::String& id, const float plain) {
        if (auto* para = p.parameters_.getParameter(id)) {
            para->setValueNotifyingHost(para->convertTo0to1(plain));
        } else {
            std::cerr << "unknown parameter " << id << std::endl;
        }
    }

    void setDisplay(PluginProcessor& p, const juce::String& id, const float plain) {
        if (auto* para = p.na_parameters_.getParameter(id)) {
            para->setValueNotifyingHost(para->convertTo0to1(plain));
        }
    }

    void resetAll(PluginProcessor& p) {
        for (auto* para : p.getParameters()) {
            para->setValueNotifyingHost(para->getDefaultValue());
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
}

int main(const int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto out_dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                  : juce::File::getCurrentWorkingDirectory();
    const auto scale_factor = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 2.f;
    out_dir.createDirectory();

    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 512;
    PluginProcessor p;
    p.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    p.prepareToPlay(kSampleRate, kBlock);

    std::unique_ptr<juce::AudioProcessorEditor> editor_base(p.createEditor());
    auto* editor = dynamic_cast<altvane::compressor::Editor*>(editor_base.get());
    if (editor == nullptr) {
        std::cerr << "editor is not an Altvane Compressor editor" << std::endl;
        return 1;
    }
    editor->setSize(1040, 640);

    DrumLoop loop(kSampleRate);
    juce::AudioBuffer<float> buffer(4, kBlock);
    juce::MidiBuffer midi;
    double t = 0.0;
    double audio_time = 0.0;
    auto run = [&](const double seconds) {
        const auto frames = static_cast<int>(seconds * 60.0);
        for (int f = 0; f < frames; ++f) {
            t += 1.0 / 60.0;
            while (audio_time < t) {
                for (int i = 0; i < kBlock; ++i) {
                    const auto s = loop.next();
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s * .92f);
                    // the external side chain: the same loop, quieter
                    buffer.setSample(2, i, s * .5f);
                    buffer.setSample(3, i, s * .5f);
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

    // a drum bus setting
    set(p, zlp::PThreshold::kID, -24.f);
    set(p, zlp::PRatio::kID, 4.f);
    set(p, zlp::PKneeW::kID, 6.f);
    set(p, zlp::PAttack::kID, 10.f);
    set(p, zlp::PRelease::kID, 120.f);
    set(p, zlp::POutGain::kID, 4.f);
    // long enough for the history to fill the display
    run(10.0);
    // the README's picture (docs/screenshot.png, at scale 2)
    writePNG(*editor, out_dir.getChildFile("00_screenshot.png"), scale_factor);
    writePNG(*editor, out_dir.getChildFile("01_default.png"), scale_factor);

    // heavy: fast and deep
    set(p, zlp::PThreshold::kID, -36.f);
    set(p, zlp::PRatio::kID, 10.f);
    set(p, zlp::PAttack::kID, 1.f);
    set(p, zlp::PRelease::kID, 60.f);
    set(p, zlp::POutGain::kID, 10.f);
    run(4.0);
    writePNG(*editor, out_dir.getChildFile("02_heavy.png"), scale_factor);

    // upward: inflate quiet parts
    resetAll(p);
    set(p, zlp::PCompDirection::kID, 1.f);
    set(p, zlp::PThreshold::kID, -30.f);
    set(p, zlp::PRatio::kID, 2.f);
    run(4.0);
    writePNG(*editor, out_dir.getChildFile("03_inflate.png"), scale_factor);

    // the side chain with a high-pass and a presence boost
    resetAll(p);
    set(p, zlp::PThreshold::kID, -30.f);
    set(p, zlp::PRatio::kID, 4.f);
    set(p, zlp::PFilterType::kID + juce::String(0), static_cast<float>(zldsp::filter::FilterType::kHighPass));
    set(p, zlp::POrder::kID + juce::String(0), 2.f);
    set(p, zlp::PFreq::kID + juce::String(0), 140.f);
    set(p, zlp::PFilterStatus::kID + juce::String(0), 2.f);
    set(p, zlp::PFilterType::kID + juce::String(1), static_cast<float>(zldsp::filter::FilterType::kPeak));
    set(p, zlp::PFreq::kID + juce::String(1), 6800.f);
    set(p, zlp::PGain::kID + juce::String(1), 9.f);
    set(p, zlp::PQ::kID + juce::String(1), 2.2f);
    set(p, zlp::PFilterStatus::kID + juce::String(1), 2.f);
    setDisplay(p, zlstate::PSideControlDisplay::kID, 1.f);
    run(4.0);
    writePNG(*editor, out_dir.getChildFile("04_side_chain.png"), scale_factor);
    // the presence band selected, with its values along the top, then soloed
    auto& side_eq = editor->getSideChainForTesting().getEqForTesting();
    side_eq.select(1);
    run(1.0);
    writePNG(*editor, out_dir.getChildFile("04b_side_chain_band.png"), scale_factor);
    side_eq.setSolo(1);
    run(1.0);
    writePNG(*editor, out_dir.getChildFile("04c_side_chain_solo.png"), scale_factor);
    side_eq.setSolo(zlp::kBandNum);
    side_eq.select(-1);
    setDisplay(p, zlstate::PSideControlDisplay::kID, 0.f);
    run(.6);

    // the advanced controls
    resetAll(p);
    set(p, zlp::PRMSON::kID, 1.f);
    editor->setAdvancedOpen(true);
    run(1.0);
    writePNG(*editor, out_dir.getChildFile("05_advanced.png"), scale_factor);
    editor->setAdvancedOpen(false);

    // bypassed
    set(p, zlp::PCompON::kID, 0.f);
    run(2.0);
    writePNG(*editor, out_dir.getChildFile("06_bypassed.png"), scale_factor);
    set(p, zlp::PCompON::kID, 1.f);

    // a help balloon
    editor->getHelpForTesting().showFor(editor->getCardForTesting().getMoreButton());
    run(.5);
    writePNG(*editor, out_dir.getChildFile("06b_help.png"), scale_factor);
    editor->getHelpForTesting().hide();

    editor->openPresetsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("07_presets.png"), scale_factor);
    editor->closeSheetsForTesting();
    editor->openSettingsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("08_settings.png"), scale_factor);
    editor->closeSheetsForTesting();
    run(.4);

    // the smallest window
    editor->setSize(960, 560);
    run(2.0);
    writePNG(*editor, out_dir.getChildFile("09_small_window.png"), scale_factor);

    editor_base.reset();
    p.releaseResources();
    return 0;
}
