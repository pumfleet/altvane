// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "output_rail.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"

namespace altvane::ui {
    using namespace tokens;

    namespace {
        juce::Font captionFont() {
            return Fonts::get().font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f);
        }
    }

    OutputRail::OutputRail(const juce::AudioProcessor& processor, std::function<std::array<float, 2>()> take_peaks) :
        processor_(processor), take_peaks_(std::move(take_peaks)) {
        setTitle("Output meter");
    }

    void OutputRail::resized() {
        auto b = getLocalBounds().toFloat().reduced(6.f, 8.f);
        label_area_ = b.removeFromTop(16.f);
        b.removeFromTop(8.f);
        peak_area_ = b.removeFromBottom(16.f);
        b.removeFromBottom(6.f);
        meter_area_ = b.withSizeKeepingCentre(14.f, b.getHeight());
    }

    void OutputRail::tick(const double dt) {
        const auto peaks = take_peaks_();
        if (meter_.tick(dt, peaks[0], processor_.getMainBusNumOutputChannels() > 1 ? peaks[1] : peaks[0])) {
            repaint();
        }
    }

    void OutputRail::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, 1.f, b.getHeight()));
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("OUT", label_area_, juce::Justification::centred);
        meter_.paint(g, meter_area_, peak_area_);
    }

    void OutputRail::mouseUp(const juce::MouseEvent& e) {
        if (meter_area_.expanded(8.f, 0.f).contains(e.position) || peak_area_.contains(e.position)) {
            meter_.resetPeak();
            repaint();
        }
    }
}
