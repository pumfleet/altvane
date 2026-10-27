// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "chrome.hpp"

#include <altvane_ui/util/value_format.hpp>

#include "theme/leveler_tokens.hpp"

namespace altvane::leveler {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        void drawCaptioned(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& caption,
                           const juce::String& value, const juce::Justification side) {
            const auto value_w = Fonts::get().getTabularWidth(value, type::kBody);
            const auto caption_w = juce::GlyphArrangement::getStringWidth(captionFont(), caption);
            if (side == juce::Justification::centredRight) {
                area = area.removeFromRight(value_w + caption_w + size::kS2);
            }
            g.setFont(captionFont());
            g.setColour(colour::kTextTertiary);
            g.drawText(caption, area.removeFromLeft(caption_w), juce::Justification::centredLeft);
            area.removeFromLeft(size::kS2);
            g.setColour(colour::kTextSecondary);
            Fonts::get().drawTabular(g, value, area, juce::Justification::centredLeft, type::kBody);
        }
    }

    // ---------------------------------------------------------------- Footer

    Footer::Footer(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        ui::bindValue(output_, editor_, p.parameters_.getParameter(params::kOutput), "Output");
        output_.format = [](const float v) { return ui::format::gain(v); };
        addAndMakeVisible(output_);

        bypass_.setToggleable(true);
        bypass_.setComponentID(params::kBypass);
        bypass_.setTooltip("Bypass the plug-in");
        bypass_.setOnColour(colour::kWarning);
        bypass_.on_click = [this] { editor_.toggle(p_ref_.parameters_.getParameter(params::kBypass), "Bypass"); };
        addAndMakeVisible(bypass_);
        tick();
    }

    void Footer::tick() {
        const auto& tree = p_ref_.parameters_;
        output_.setValue(tree.getRawParameterValue(params::kOutput)->load());
        bypass_.setOn(tree.getRawParameterValue(params::kBypass)->load() > .5f);

        const auto key_channels = p_ref_.getKeyChannels();
        const auto key = key_channels == 0 ? juce::String("Not connected") : key_channels == 1 ? juce::String("Mono") : juce::String("Stereo");
        const auto rate = p_ref_.getSampleRate();
        const auto latency = rate > 0.0 ? juce::String(1000.0 * p_ref_.getLatencySamples() / rate, 1) + " ms" : juce::String("--");
        if (key != key_text_ || latency != latency_text_) {
            key_text_ = key;
            latency_text_ = latency;
            repaint();
        }
    }

    void Footer::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, 6.f);
        key_area_ = b.removeFromLeft(220.f);
        b.removeFromRight(10.f); // window resize grip
        bypass_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(size::kS2);
        output_.setBounds(b.removeFromRight(118.f).toNearestInt());
        b.removeFromRight(size::kS4);
        latency_area_ = b.removeFromRight(120.f);
    }

    void Footer::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, b.getWidth(), 1.f));
        drawCaptioned(g, key_area_, "MUSIC KEY", key_text_, juce::Justification::centredLeft);
        drawCaptioned(g, latency_area_, "LATENCY", latency_text_, juce::Justification::centredRight);
    }

    // ---------------------------------------------------------------- OutputRail

    OutputRail::OutputRail(PluginProcessor& p) : p_ref_(p) {
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
        const auto peaks = p_ref_.takeOutputPeaks();
        if (meter_.tick(dt, peaks[0], p_ref_.getTotalNumOutputChannels() > 1 ? peaks[1] : peaks[0])) {
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
