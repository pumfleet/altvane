// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.

// Renders the Contour editor headlessly with the real processor running on synthetic audio,
// and writes PNG snapshots for visual review.
//
// usage: ContourSnapshot <output-dir> [scale]

#include "PluginProcessor.hpp"
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "altvane_ui/theme/look_and_feel.hpp"
#include "ui/editor.hpp"
#include "ui/theme/eq_tokens.hpp"

namespace {
    class Signal {
    public:
        explicit Signal(const double sample_rate) : sample_rate_(sample_rate) {
        }

        // pink noise plus a few tonal partials, roughly like a mix bus
        float next() {
            const auto white = random_.nextFloat() * 2.f - 1.f;
            b0_ = .99886f * b0_ + white * .0555179f;
            b1_ = .99332f * b1_ + white * .0750759f;
            b2_ = .96900f * b2_ + white * .1538520f;
            b3_ = .86650f * b3_ + white * .3104856f;
            b4_ = .55000f * b4_ + white * .5329522f;
            b5_ = -.7616f * b5_ - white * .0168980f;
            const auto pink = (b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * .5362f) * .06f;
            b6_ = white * .115926f;
            float tones = 0.f;
            for (size_t i = 0; i < kFreqs.size(); ++i) {
                phases_[i] += kFreqs[i] / sample_rate_;
                phases_[i] -= std::floor(phases_[i]);
                tones += kAmps[i] * static_cast<float>(std::sin(phases_[i] * juce::MathConstants<double>::twoPi));
            }
            return pink + tones;
        }

    private:
        static constexpr std::array<double, 4> kFreqs{55.0, 110.0, 440.0, 2600.0};
        static constexpr std::array<float, 4> kAmps{.12f, .06f, .03f, .015f};
        double sample_rate_;
        juce::Random random_{1234};
        float b0_{}, b1_{}, b2_{}, b3_{}, b4_{}, b5_{}, b6_{};
        std::array<double, 4> phases_{};
    };

    void set(PluginProcessor& p, const juce::String& id, const float plain) {
        if (auto* para = p.parameters_.getParameter(id)) {
            para->setValueNotifyingHost(para->convertTo0to1(plain));
        } else {
            std::cerr << "unknown parameter " << id << std::endl;
        }
    }

    void band(PluginProcessor& p, const int idx, const zldsp::filter::FilterType type, const float freq,
              const float gain, const float q, const int order_idx = 1) {
        const auto s = juce::String(idx);
        set(p, zlp::PFilterType::kID + s, static_cast<float>(type));
        set(p, zlp::POrder::kID + s, static_cast<float>(order_idx));
        set(p, zlp::PFreq::kID + s, freq);
        set(p, zlp::PGain::kID + s, gain);
        set(p, zlp::PTargetGain::kID + s, gain);
        set(p, zlp::PQ::kID + s, q);
        set(p, zlp::PFilterStatus::kID + s, static_cast<float>(zlp::kOn));
    }

    /**
     * pop-up menus open in their own windows, which cannot be captured headlessly;
     * draw a menu with the editor's look-and-feel instead
     */
    void writeMenuPreview(const juce::File& file, const float scale) {
        altvane::eq::LookAndFeel laf;
        juce::PopupMenu menu;
        menu.addSectionHeader("Processing");
        const char* items[] = {"Minimum\tZero latency", "SVF\tZero latency, smooth automation",
                               "Parallel\tZero latency, natural dynamics", "", "Matched\tAnalog phase, latency",
                               "Mixed\tClean high end, latency", "Linear\tNo phase shift, latency"};
        int id = 1;
        for (const auto* text : items) {
            if (juce::String(text).isEmpty()) {
                menu.addSeparator();
            } else {
                menu.addItem(id, text, true, id == 1);
                ++id;
            }
        }
        const juce::PopupMenu::Options options;
        int width = 0, height = 10;
        std::vector<std::pair<const juce::PopupMenu::Item*, int>> rows;
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
            auto& item = it.getItem();
            int w = 0, h = 0;
            if (item.isSectionHeader) {
                w = 120;
                h = 26;
            } else {
                laf.getIdealPopupMenuItemSizeWithOptions(item.text, item.isSeparator, 24, w, h, options);
            }
            width = std::max(width, w);
            rows.emplace_back(&item, h);
            height += h;
        }
        juce::Image image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(width + 40) * scale),
                          juce::roundToInt(static_cast<float>(height + 40) * scale), true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.fillAll(altvane::eq::tokens::colour::kGraph);
        g.setOrigin(20, 20);
        laf.drawPopupMenuBackgroundWithOptions(g, width, height, options);
        int y = 5;
        bool highlight = true;
        for (const auto& [item, h] : rows) {
            const auto area = juce::Rectangle<int>(0, y, width, h);
            if (item->isSectionHeader) {
                laf.drawPopupMenuSectionHeaderWithOptions(g, area, item->text, options);
            } else {
                // show the hover state on the second entry
                laf.drawPopupMenuItemWithOptions(g, area, highlight && item->itemID == 2, *item, options);
            }
            y += h;
        }
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
        std::cout << "wrote " << file.getFullPathName() << std::endl;
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

    using FT = zldsp::filter::FilterType;
    band(p, 0, FT::kHighPass, 32.f, 0.f, .707f, 2);
    band(p, 1, FT::kLowShelf, 110.f, 2.5f, .707f);
    band(p, 2, FT::kPeak, 340.f, -4.f, 1.6f);
    band(p, 3, FT::kPeak, 2900.f, 3.f, .9f);
    band(p, 4, FT::kHighShelf, 9500.f, 2.f, .707f);
    band(p, 5, FT::kNotch, 6200.f, 0.f, 6.f);
    set(p, zlp::PDynamicON::kID + juce::String(3), 1.f);
    set(p, zlp::PTargetGain::kID + juce::String(3), -2.f);
    set(p, zlp::PThreshold::kID + juce::String(3), -42.f);
    set(p, zlp::PFilterStatus::kID + juce::String(5), static_cast<float>(zlp::kBypass));

    std::unique_ptr<juce::AudioProcessorEditor> editor_base(p.createEditor());
    auto* editor = dynamic_cast<altvane::eq::Editor*>(editor_base.get());
    if (editor == nullptr) {
        std::cerr << "editor is not a Contour editor" << std::endl;
        return 1;
    }
    editor->setSize(1040, 620);

    Signal signal(kSampleRate);
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
                    const auto s = signal.next();
                    buffer.setSample(0, i, s);
                    buffer.setSample(1, i, s * .9f);
                    buffer.setSample(2, i, s * .5f);
                    buffer.setSample(3, i, s * .5f);
                }
                p.processBlock(buffer, midi);
                audio_time += kBlock / kSampleRate;
            }
            editor->runFrameForTesting(t);
#if JUCE_MAC
            // pump the message queue (async callbacks, repaints)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, .004, false);
#endif
            juce::Thread::sleep(4);
        }
    };

    writeMenuPreview(out_dir.getChildFile("00_menu.png"), scale_factor);
    run(3.0);
    writePNG(*editor, out_dir.getChildFile("01_overview.png"), scale_factor);

    editor->getSelectionForTesting().select(3);
    run(.8);
    writePNG(*editor, out_dir.getChildFile("02_dynamic_band_selected.png"), scale_factor);

    editor->getSelectionForTesting().select(2);
    run(.8);
    writePNG(*editor, out_dir.getChildFile("03_bell_selected.png"), scale_factor);

    // analyzer extras: piano roll, collision against the side-chain, Spectrum Grab hover on the 440 Hz partial
    {
        auto setNA = [&](const char* id, const float v) {
            auto* para = p.parameters_NA_.getParameter(id);
            para->setValueNotifyingHost(para->convertTo0to1(v));
        };
        setNA(zlstate::PPianoRollON::kID, 1.f);
        setNA(zlstate::PCollisionON::kID, 1.f);
        setNA(zlstate::PCollisionStrength::kID, 1.f);
        setNA(zlstate::PFFTSideON::kID, 1.f);
        editor->getSelectionForTesting().clear();
        run(1.5);
        auto& graph = editor->getGraphForTesting();
        altvane::eq::FreqScale scale;
        scale.setBounds(0.f, static_cast<float>(graph.getWidth()));
        graph.hoverForTesting({scale.freqToX(440.0), static_cast<float>(graph.getHeight()) * .72f});
        run(.4);
        writePNG(*editor, out_dir.getChildFile("06_analyzer_extras.png"), scale_factor);
        setNA(zlstate::PPianoRollON::kID, 0.f);
        setNA(zlstate::PCollisionON::kID, 0.f);
        setNA(zlstate::PFFTSideON::kID, 0.f);
    }

    // overlays
    editor->openPresetsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("07_preset_browser.png"), scale_factor);
    editor->closeSheetsForTesting();
    editor->openSettingsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("08_settings.png"), scale_factor);
    editor->closeSheetsForTesting();
    run(.4);

    // EQ Match against the natural reference: listen, then fit
    {
        auto& match = editor->getMatchForTesting();
        match.start();
        match.setTarget(altvane::eq::MatchEngine::Target::kNatural);
        run(3.0);
        writePNG(*editor, out_dir.getChildFile("09_match_learning.png"), scale_factor);
        match.fit();
        run(4.0);
        writePNG(*editor, out_dir.getChildFile("10a_match_fitting.png"), scale_factor);
        for (int i = 0; i < 240 && match.getState() != altvane::eq::MatchEngine::State::kResult; ++i) {
            run(.5);
        }
        run(.5);
        writePNG(*editor, out_dir.getChildFile("10_match_result.png"), scale_factor);
        std::cout << "match state " << static_cast<int>(match.getState()) << " frame valid " << match.getFrame().valid
            << " seconds " << match.getFrame().seconds << " corr[64] " << match.getFrame().correction[64] << std::endl;
        std::cout << "match: " << match.getNumApplied() << " bands applied, suggested " << match.getSuggested()
            << " of " << match.getMaxBands() << std::endl;
        match.cancel();
        run(.5);
    }

    // empty state
    for (int i = 0; i < 6; ++i) {
        set(p, zlp::PFilterStatus::kID + juce::String(i), 0.f);
    }
    editor->getSelectionForTesting().clear();
    run(1.0);
    writePNG(*editor, out_dir.getChildFile("04_empty.png"), scale_factor);

    editor->setSize(800, 480);
    run(.3);
    writePNG(*editor, out_dir.getChildFile("05_small_window.png"), scale_factor);

    editor_base.reset();
    p.releaseResources();
    return 0;
}
