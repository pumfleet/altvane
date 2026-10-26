// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
// Renders an Altvane product's application icon and installer artwork as PNGs.
// usage: AltvaneIcons <output-dir> <product-label>   (see make_icons.sh, which assembles .icns / .ico / .bmp)

#include <juce_gui_basics/juce_gui_basics.h>

#include "AltvaneUIBinary.h"
#include "altvane_ui/theme/brand.hpp"

namespace {
    const juce::Colour kAccent{0xFF47D2E8};
    const juce::Colour kBackgroundTop{0xFF1B2027};
    const juce::Colour kBackgroundBottom{0xFF0A0B0D};

    /**
     * the Altvane mark, the same as the editor header's, with a soft glow
     */
    void drawMark(juce::Graphics& g, const juce::Rectangle<float> area, const float stroke) {
        altvane::ui::brand::drawMark(g, area, kAccent.withAlpha(.07f), stroke * 2.4f);
        altvane::ui::brand::drawMark(g, area, kAccent, stroke);
    }

    juce::Image renderIcon(const int size) {
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
        // a quiet frequency grid
        g.saveState();
        g.reduceClipRegion(tile.toNearestInt());
        g.setColour(juce::Colours::white.withAlpha(.045f));
        for (int i = 1; i < 4; ++i) {
            const auto y = tile.getY() + tile.getHeight() * static_cast<float>(i) / 4.f;
            g.fillRect(juce::Rectangle<float>(tile.getX(), y, tile.getWidth(), std::max(1.f, s / 512.f)));
            const auto x = tile.getX() + tile.getWidth() * static_cast<float>(i) / 4.f;
            g.fillRect(juce::Rectangle<float>(x, tile.getY(), std::max(1.f, s / 512.f), tile.getHeight()));
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
        std::cerr << "usage: AltvaneIcons <output-dir> <product-label>" << std::endl;
        return 1;
    }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const auto product = juce::String(argv[2]);
    dir.createDirectory();
    for (const auto size : {16, 24, 32, 48, 64, 128, 256, 512, 1024}) {
        write(renderIcon(size), dir.getChildFile("icon_" + juce::String(size) + ".png"));
    }
    write(renderInstallerArt(493, 58, true, product), dir.getChildFile("banner.png"));
    write(renderInstallerArt(493, 312, false, product), dir.getChildFile("dialog.png"));
    return 0;
}
