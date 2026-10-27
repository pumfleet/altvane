// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
// Renders an Altvane product's application icon and installer artwork as PNGs.
// usage: AltvaneIcons <output-dir> <product-label> [grid|knee|ride|ess|ceiling]   (see make_icons.sh, which assembles .icns / .ico / .bmp)

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
     * transfer curve over a level grid (Compressor), a level riding onto a target line (Leveler), a voice's
     * waveform with an ess held under a line (De-Esser) or a waveform held under a flat ceiling (Limiter)
     */
    enum class Motif { kGrid, kKnee, kRide, kEss, kCeiling };

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
        if (motif == Motif::kRide) {
            // an uneven level, and the same level ridden onto the target line below the mark
            const auto at = [&](const float u, const float v) {
                return juce::Point<float>(tile.getX() + tile.getWidth() * u, tile.getBottom() - tile.getHeight() * v);
            };
            juce::Path raw, ridden;
            raw.startNewSubPath(at(0.f, .1f));
            ridden.startNewSubPath(at(0.f, .14f));
            for (int i = 1; i <= 40; ++i) {
                const auto u = static_cast<float>(i) / 40.f;
                const auto swing = std::sin(u * 9.f) * .045f + std::sin(u * 23.f + 1.f) * .015f;
                raw.lineTo(at(u, .12f + swing));
                ridden.lineTo(at(u, .14f + swing * .18f));
            }
            const auto stroke = juce::PathStrokeType(std::max(1.f, s / 160.f), juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded);
            g.setColour(juce::Colours::white.withAlpha(.05f));
            g.strokePath(raw, stroke);
            g.setColour(juce::Colours::white.withAlpha(.11f));
            g.strokePath(ridden, stroke);
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
        if (motif == Motif::kCeiling) {
            // a level envelope whose peaks flatten against a ceiling line
            const auto at = [&](const float u, const float v) {
                return juce::Point<float>(tile.getX() + tile.getWidth() * u, tile.getBottom() - tile.getHeight() * v);
            };
            constexpr float kCeilingLevel = .62f;
            constexpr std::array kPeaks{.3f, .55f, .8f, .42f, .9f, .5f, .75f, .35f, .85f, .6f, .95f, .45f};
            juce::Path envelope;
            envelope.startNewSubPath(at(0.f, .12f));
            for (size_t i = 0; i < kPeaks.size(); ++i) {
                const auto u = (static_cast<float>(i) + .5f) / static_cast<float>(kPeaks.size());
                const auto half = .5f / static_cast<float>(kPeaks.size());
                const auto peak = std::min(kPeaks[i], kCeilingLevel);
                envelope.lineTo(at(u - half * .55f, peak));
                envelope.lineTo(at(u + half * .25f, peak));
                envelope.lineTo(at(u + half, .14f + .05f * static_cast<float>(i % 3)));
            }
            const auto stroke = std::max(1.f, s / 200.f);
            g.setColour(juce::Colours::white.withAlpha(.09f));
            g.strokePath(envelope, juce::PathStrokeType(stroke, juce::PathStrokeType::mitered,
                                                        juce::PathStrokeType::rounded));
            g.setColour(kAccent.withAlpha(.22f));
            g.fillRect(juce::Rectangle<float>(tile.getX(), at(0.f, kCeilingLevel).y - stroke * .5f, tile.getWidth(),
                                              stroke));
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

    /**
     * a 24-bit BMP for the WiX installer art, for systems without sips
     */
    void writeBMP(const juce::Image& image, const juce::File& file) {
        file.deleteFile();
        juce::FileOutputStream out(file);
        const auto w = image.getWidth(), h = image.getHeight();
        const auto row = (w * 3 + 3) / 4 * 4;
        const auto data_size = static_cast<uint32_t>(row * h);
        out.write("BM", 2);
        out.writeInt(static_cast<int>(54 + data_size));
        out.writeInt(0);
        out.writeInt(54);
        out.writeInt(40);
        out.writeInt(w);
        out.writeInt(h);
        out.writeShort(1);
        out.writeShort(24);
        out.writeInt(0);
        out.writeInt(static_cast<int>(data_size));
        out.writeInt(2835);
        out.writeInt(2835);
        out.writeInt(0);
        out.writeInt(0);
        // bottom-up rows of blue, green, red
        for (int y = h - 1; y >= 0; --y) {
            for (int x = 0; x < w; ++x) {
                const auto c = image.getPixelAt(x, y);
                out.writeByte(static_cast<char>(c.getBlue()));
                out.writeByte(static_cast<char>(c.getGreen()));
                out.writeByte(static_cast<char>(c.getRed()));
            }
            for (int p = w * 3; p < row; ++p) {
                out.writeByte(0);
            }
        }
        std::cout << "wrote " << file.getFileName() << std::endl;
    }
}

int main(const int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) {
        std::cerr << "usage: AltvaneIcons <output-dir> <product-label> [grid|knee|ride|ess|ceiling]" << std::endl;
        return 1;
    }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const auto product = juce::String(argv[2]);
    const auto motif_name = argc > 3 ? juce::String(argv[3]) : juce::String("grid");
    const auto motif = motif_name == "knee"      ? Motif::kKnee
                       : motif_name == "ride"    ? Motif::kRide
                       : motif_name == "ess"     ? Motif::kEss
                       : motif_name == "ceiling" ? Motif::kCeiling
                                                 : Motif::kGrid;
    dir.createDirectory();
    for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
        write(renderIcon(size, motif), dir.getChildFile("icon_" + juce::String(size) + ".png"));
    }
    const auto banner = renderInstallerArt(493, 58, true, product);
    const auto dialog = renderInstallerArt(493, 312, false, product);
    write(banner, dir.getChildFile("banner.png"));
    write(dialog, dir.getChildFile("dialog.png"));
    writeBMP(banner, dir.getChildFile("banner.bmp"));
    writeBMP(dialog, dir.getChildFile("dialog.bmp"));
    return 0;
}
