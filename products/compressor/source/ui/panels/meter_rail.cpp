// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "meter_rail.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/util/value_format.hpp"
#include "../graph/graph_helpers.hpp"
#include "../model/formats.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;

    namespace {
        // level meters fall at this rate (dB per second)
        constexpr float kFall = 24.f;
        constexpr float kTop = 34.f, kBottomPad = 26.f;

        juce::String peakText(const float v) {
            if (v <= -119.f) {
                return ui::format::kMinus + juce::String::fromUTF8("\xe2\x88\x9e");
            }
            return formats::number(v, std::abs(v) >= 10.f ? 0 : 1);
        }
    }

    MeterRail::MeterRail(CompBridge& bridge) : bridge_(bridge) {
        setOpaque(true);
        setTitle("Meters");
        setDescription("Input, gain change and output. Click to reset the peaks.");
    }

    void MeterRail::resetPeaks() {
        input_peak_ = -120.f;
        output_peak_ = -120.f;
        gain_peak_ = 0.f;
        repaint();
    }

    void MeterRail::tick(const double dt, const LevelHistory::Meters& meters) {
        scale_db_ = bridge_.getScaleDB();
        const auto fall = kFall * static_cast<float>(dt);
        for (size_t ch = 0; ch < 2; ++ch) {
            input_[ch] = std::max(meters.input[ch], input_[ch] - fall);
            output_[ch] = std::max(meters.output[ch], output_[ch] - fall);
        }
        gain_ = std::abs(meters.gain) >= std::abs(gain_)
                    ? meters.gain
                    : gain_ * std::pow(.02f, static_cast<float>(dt));
        input_peak_ = std::max({input_peak_, meters.input[0], meters.input[1]});
        output_peak_ = std::max({output_peak_, meters.output[0], meters.output[1]});
        if (std::abs(meters.gain) > std::abs(gain_peak_)) {
            gain_peak_ = meters.gain;
        }
        repaint();
    }

    void MeterRail::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.fillAll(colour::kWindow);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, 1.f, b.getHeight()));

        const auto upward = bridge_.isUpward();
        const auto gain_colour = upward ? colour::kUpward : colour::kReduction;
        const auto top = kTop;
        const auto bottom = b.getHeight() - kBottomPad;
        const auto h = bottom - top;
        const auto yOf = [&](const float db) { return top + std::clamp(-db / scale_db_, 0.f, 1.f) * h; };

        // three columns centred in the rail
        const auto cx = b.getCentreX();
        const auto in_x = cx - 22.f, gr_x = cx, out_x = cx + 24.f;

        // peak readouts
        const auto readout = [&](const float x, const juce::Colour c, const juce::String& text) {
            const auto area = juce::Rectangle<float>(23.f, 16.f).withCentre({x, 17.5f});
            g.setColour(juce::Colour{0x0AFFFFFF});
            g.fillRoundedRectangle(area, 3.f);
            g.setColour(c);
            Fonts::get().drawTabular(g, text, area, juce::Justification::centred, 9.5f);
        };
        readout(in_x, colour::kTextPrimary.withAlpha(.75f), peakText(input_peak_));
        readout(gr_x, gain_colour, formats::number(std::abs(gain_peak_), 1));
        readout(out_x, output_peak_ > -.1f ? colour::kDanger : colour::kTextPrimary,
                peakText(output_peak_));

        // level bars
        const auto bar = [&](const float x, const float db) {
            const auto area = juce::Rectangle<float>(x, top, 5.f, h);
            g.setColour(juce::Colour{0x0DFFFFFF});
            g.fillRect(area);
            const auto y = yOf(db);
            g.setGradientFill(juce::ColourGradient(colour::kMeterTop, 0.f, top, colour::kMeterBottom, 0.f, bottom,
                                                   false));
            g.fillRect(juce::Rectangle<float>(x, y, 5.f, bottom - y));
        };
        bar(in_x - 6.f, input_[0]);
        bar(in_x + 1.f, input_[1]);
        bar(out_x - 6.f, output_[0]);
        bar(out_x + 1.f, output_[1]);

        // gain change from the top
        const auto gr_area = juce::Rectangle<float>(gr_x - 4.f, top, 8.f, h);
        g.setColour(juce::Colour{0x0DFFFFFF});
        g.fillRect(gr_area);
        g.setColour(gain_colour);
        g.fillRect(gr_area.withHeight(std::clamp(std::abs(gain_) / scale_db_, 0.f, 1.f) * h));
        g.setColour(gain_colour.withAlpha(.9f));
        g.fillRect(juce::Rectangle<float>(gr_area.getX(), top + std::clamp(std::abs(gain_peak_) / scale_db_, 0.f, 1.f)
                                                              * h - 1.f, 8.f, 1.5f));

        // the threshold on the input meter
        const auto threshold = bridge_.get(zlp::PThreshold::kID);
        if (threshold > -scale_db_) {
            const auto ty = yOf(threshold);
            juce::Path marker;
            marker.addTriangle(in_x + 8.f, ty, in_x + 12.f, ty - 3.f, in_x + 12.f, ty + 3.f);
            g.setColour(colour::kAccent);
            g.fillPath(marker);
        }

        g.setFont(Fonts::get().font(9.f, Fonts::kSemiBold));
        g.setColour(juce::Colour{0x61FFFFFF});
        const auto label_y = b.getHeight() - 20.f;
        g.drawText("IN", juce::Rectangle<float>(in_x - 12.f, label_y, 24.f, 14.f), juce::Justification::centred);
        g.drawText(upward ? "GAIN" : "GR", juce::Rectangle<float>(gr_x - 14.f, label_y, 28.f, 14.f),
                   juce::Justification::centred);
        g.drawText("OUT", juce::Rectangle<float>(out_x - 12.f, label_y, 24.f, 14.f), juce::Justification::centred);
    }

    void MeterRail::mouseUp(const juce::MouseEvent& e) {
        if (!e.mods.isPopupMenu()) {
            resetPeaks();
        }
    }
}
