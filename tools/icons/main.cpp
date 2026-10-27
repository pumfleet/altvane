// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
// Renders an Altvane product's application icon and installer artwork as PNGs.
// usage: AltvaneIcons <output-dir> <product-label> [grid|knee]   (see make_icons.sh, which assembles .icns / .ico / .bmp)

#include <juce_gui_basics/juce_gui_basics.h>

#include "AltvaneUIBinary.h"
#include "altvane_ui/theme/brand.hpp"

namespace {
    const juce::Colour kAccent{0xFF47D2E8};
    const juce::Colour kBackgroundTop{0xFF1B2027};
    const juce::Colour kBackgroundBottom{0xFF0A0B0D};
    const juce::Colour kSibilance{0xFFFFB27A};

    /**
     * the quiet drawing behind the mark that tells the products apart: a frequency grid (EQ), a compressor's
     * transfer curve over a level grid (Compressor) or a voice's waveform with an ess held under a line (De-Esser)
     */
    enum class Motif { kGrid, kKnee, kEss };

    /**
     * the Altvane mark, the same as the editor header's, with a soft glow
     */
    void drawMark(juce::Graphics& g, const juce::Rectangle<float> area, const float stroke) {
        altvane::ui::brand::drawMark(g, area, kAccent.withAlpha(.07f), stroke * 2.4f);
        altvane::ui::brand::drawMark(g, area, kAccent, stroke);
    }

    juce::Image renderIcon(const int size, const Motif motif) {
        juce::Image image(juce::Image::ARGB, size, size, true);
        juce::Graphics g(image);
        const auto s = static_cast<float>(size);
        // macOS icon grid: ~10% margin, continuous-corner-like radius
        const auto tile = juce::Rectangle<float>(s, s).reduced(s * .098f);
        const auto radius = tile.getWidth() * .2237f;
        g.setColour(juce::Colours::black.withAlpha(.35f));
        g.fillRoundedRectangle(tile.translated(0.f, s * .012f), radius);
        g.setGradientFill(juce::ColourGradient(kBackgroundTop, 0.f, tile.getY(), kBackgroundBottom, 0.f, tile.getBottom(), false));
        g.fillRoundedRectangle(tile, radius);
        // a quiet grid
        g.saveState();
        g.reduceClipRegion(tile.toNearestInt());
        g.setColour(juce::Colours::white.withAlpha(.045f));
        for (int i = 1; i < 4; ++i) {
            const auto y = tile.getY() + tile.getHeight() * static_cast<float>(i) / 4.f;
            g.fillRect(juce::Rectangle<float>(tile.getX(), y, tile.getWidth(), std::max(1.f, s / 512.f)));
            const auto x = tile.getX() + tile.getWidth() * static_cast<float>(i) / 4.f;
            g.fillRect(juce::Rectangle<float>(x, tile.getY(), std::max(1.f, s / 512.f), tile.getHeight()));
        }
        if (motif == Motif::kKnee) {
            // input level against output level: 1:1 up to a soft knee, then a flatter slope
            const auto at = [&](const float u, const float v) {
                return juce::Point<float>(tile.getX() + tile.getWidth() * u, tile.getBottom() - tile.getHeight() * v);
            };
            juce::Path curve;
            // offset below the mark's diagonal so the two never overlap
            curve.startNewSubPath(at(.2f, 0.f));
            curve.lineTo(at(.6f, .4f));
            curve.quadraticTo(at(.7f, .5f), at(.8f, .53f));
            curve.lineTo(at(1.f, .59f));
            g.setColour(juce::Colours::white.withAlpha(.1f));
            g.strokePath(curve, juce::PathStrokeType(std::max(1.f, s / 160.f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
        if (motif == Motif::kEss) {
            // a voice along the bottom: a slow wave, then a dense burst whose peaks stop flat at a threshold
            const auto at = [&](const float u, const float v) {
                return juce::Point<float>(tile.getX() + tile.getWidth() * u, tile.getBottom() - tile.getHeight() * v);
            };
            const auto stroke = juce::PathStrokeType(std::max(1.f, s / 200.f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded);
            constexpr float kBase = .2f, kCeiling = .075f, kBurstStart = .48f, kBurstEnd = .82f;
            const auto trace = [&](juce::Path& path, const float from, const float to, auto&& height) {
                constexpr int kSteps = 160;
                for (int i = 0; i <= kSteps; ++i) {
                    const auto u = from + (to - from) * static_cast<float>(i) / kSteps;
                    const auto p = at(u, kBase + height(u));
                    i == 0 ? path.startNewSubPath(p) : path.lineTo(p);
                }
            };
            const auto calm = [](const float u) { return .045f * std::sin(u * 26.f) * std::sin(u * 5.f + .6f); };
            const auto burst = [=](const float u) {
                const auto env = std::sin((u - kBurstStart) / (kBurstEnd - kBurstStart) * juce::MathConstants<float>::pi);
                return std::clamp(.13f * env * std::sin(u * 260.f), -kCeiling, kCeiling);
            };
            juce::Path voice, ess;
            trace(voice, 0.f, kBurstStart, calm);
            trace(voice, kBurstEnd, 1.f, calm);
            trace(ess, kBurstStart, kBurstEnd, burst);
            g.setColour(juce::Colours::white.withAlpha(.1f));
            g.strokePath(voice, stroke);
            g.setColour(kSibilance.withAlpha(.32f));
            g.strokePath(ess, stroke);
            // the threshold the ess is held under
            g.setColour(juce::Colours::white.withAlpha(.07f));
            g.fillRect(juce::Rectangle<float>(tile.getX(), at(0.f, kBase + kCeiling + .015f).y, tile.getWidth(),
                                              std::max(1.f, s / 512.f)));
        }
        g.restoreState();
        g.setColour(juce::Colours::white.withAlpha(.08f));
        g.drawRoundedRectangle(tile.reduced(s / 512.f), radius, std::max(1.f, s / 256.f));
        drawMark(g, tile.reduced(tile.getWidth() * .24f), std::max(1.2f, s * .036f));
        return image;
    }

    juce::Image renderInstallerArt(const int w, const int h, const bool banner, const juce::String& product) {
        juce::Image image(juce::Image::RGB, w, h, true);
        juce::Graphics g(image);
        g.setGradientFill(juce::ColourGradient(kBackgroundTop, 0.f, 0.f, kBackgroundBottom, 0.f, static_cast<float>(h), false));
        g.fillAll();
        auto typeface = juce::Typeface::createSystemTypefaceFor(AltvaneUIBinary::InterSemiBold_ttf, AltvaneUIBinary::InterSemiBold_ttfSize);
        if (banner) {
            // WiX draws its own text on the left; the mark sits on the right
            drawMark(g, juce::Rectangle<float>(static_cast<float>(w) - 62.f, 14.f, 30.f, 30.f), 2.4f);
        } else {
            // the welcome dialog shows the art in its left 164 px column
            drawMark(g, juce::Rectangle<float>(54.f, 100.f, 56.f, 56.f), 4.f);
            g.setColour(juce::Colours::white.withAlpha(.92f));
            g.setFont(juce::Font(juce::FontOptions(typeface).withHeight(17.f)).withExtraKerningFactor(.16f));
            g.drawText(juce::String(altvane::ui::brand::kName).toUpperCase(), juce::Rectangle<int>(0, 176, 164, 24),
                       juce::Justification::centred);
            g.setColour(juce::Colours::white.withAlpha(.6f));
            g.setFont(juce::Font(juce::FontOptions(typeface).withHeight(15.f)).withExtraKerningFactor(.08f));
            g.drawText(product, juce::Rectangle<int>(0, 200, 164, 22), juce::Justification::centred);
        }
        return image;
    }

    void write(const juce::Image& image, const juce::File& file) {
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat().writeImageToStream(image, out);
        std::cout << "wrote " << file.getFileName() << std::endl;
    }
}

int main(const int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) {
        std::cerr << "usage: AltvaneIcons <output-dir> <product-label> [grid|knee|ess]" << std::endl;
        return 1;
    }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const auto product = juce::String(argv[2]);
    const auto motif_name = argc > 3 ? juce::String(argv[3]) : juce::String("grid");
    const auto motif = motif_name == "knee" ? Motif::kKnee : motif_name == "ess" ? Motif::kEss : Motif::kGrid;
    dir.createDirectory();
    for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
        write(renderIcon(size, motif), dir.getChildFile("icon_" + juce::String(size) + ".png"));
    }
    write(renderInstallerArt(493, 58, true, product), dir.getChildFile("banner.png"));
    write(renderInstallerArt(493, 312, false, product), dir.getChildFile("dialog.png"));
    return 0;
}
