// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "../PluginProcessor.hpp"
#include "altvane_ui/theme/brand.hpp"
#include "altvane_ui/theme/tokens.hpp"

namespace altvane::compressor {
    namespace {
        constexpr int kWidth = 1040;
        constexpr int kHeight = 640;
    }

    Editor::Editor(PluginProcessor& p) :
        AudioProcessorEditor(p), parameters_(p) {
        setLookAndFeel(&laf_);
        addAndMakeVisible(parameters_);
        setResizable(true, true);
        setResizeLimits(720, 420, 3200, 2000);
        setSize(kWidth, kHeight);
    }

    Editor::~Editor() {
        setLookAndFeel(nullptr);
    }

    void Editor::paint(juce::Graphics& g) {
        g.fillAll(tokens::colour::kWindow);
        auto header = getLocalBounds().removeFromTop(static_cast<int>(tokens::size::kHeaderHeight)).toFloat();
        brand::drawLockup(g, header.reduced(tokens::size::kS3, 0.f), "COMPRESSOR");
        g.setColour(tokens::colour::kHairline);
        g.fillRect(header.removeFromBottom(1.f));
    }

    void Editor::resized() {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(static_cast<int>(tokens::size::kHeaderHeight));
        parameters_.setBounds(bounds);
    }
}
