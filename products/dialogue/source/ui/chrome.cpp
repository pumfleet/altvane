// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "chrome.hpp"

#include <altvane_ui/theme/fonts.hpp>
#include <altvane_ui/util/value_format.hpp>

#include "theme/dialogue_tokens.hpp"

namespace altvane::dialogue {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        juce::Font captionFont() {
            return Fonts::get().font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f);
        }
    }

    // ---------------------------------------------------------------- Footer

    Footer::Footer(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        for (auto* c : std::initializer_list<juce::Component*>{&eco_, &high_, &linked_, &stereo_, &mid_side_, &removed_, &output_, &bypass_}) {
            addAndMakeVisible(c);
        }
        eco_.setTooltip("Eco: the lighter model, about a third of the CPU");
        high_.setTooltip("High: the best separation");
        eco_.on_click = [this] { setChoice(params::kQuality, 0, "Quality"); };
        high_.on_click = [this] { setChoice(params::kQuality, 1, "Quality"); };
        eco_.setComponentID(params::kQuality);
        high_.setComponentID(params::kQuality);

        linked_.setTooltip("Linked: separate the mid once and apply it to both channels; keeps the stereo image");
        stereo_.setTooltip("Stereo: separate each channel on its own, at twice the CPU");
        mid_side_.setTitle("Mid/Side");
        mid_side_.setTooltip("Mid/Side: separate the mid and the side on their own, at twice the CPU; "
                             "for centred dialogue with wide ambience");
        linked_.on_click = [this] { setChoice(params::kStereo, 0, "Stereo Mode"); };
        stereo_.on_click = [this] { setChoice(params::kStereo, 1, "Stereo Mode"); };
        mid_side_.on_click = [this] { setChoice(params::kStereo, 2, "Stereo Mode"); };
        linked_.setComponentID(params::kStereo);
        stereo_.setComponentID(params::kStereo);
        mid_side_.setComponentID(params::kStereo);

        removed_.setTooltip("Hear what is being removed");
        removed_.setOnColour(colour::kWarning);
        removed_.on_click = [this] {
            p_ref_.setListen(p_ref_.getListen() == dsp::Listen::kRemoved ? dsp::Listen::kMix : dsp::Listen::kRemoved);
        };

        ui::bindValue(output_, editor_, p.parameters_.getParameter(params::kOutput), "Output");
        output_.format = [](const float v) { return ui::format::gain(v); };

        bypass_.setToggleable(true);
        bypass_.setComponentID(params::kBypass);
        bypass_.setTooltip("Bypass the plug-in");
        bypass_.setOnColour(colour::kWarning);
        bypass_.on_click = [this] {
            editor_.toggle(p_ref_.parameters_.getParameter(params::kBypass), "Bypass");
        };
        tick();
    }

    void Footer::setChoice(const char* id, const int index, const juce::String& name) {
        editor_.setNow({{p_ref_.parameters_.getParameter(id), static_cast<float>(index)}}, "Change " + name);
    }

    void Footer::tick() {
        const auto& tree = p_ref_.parameters_;
        const auto quality = juce::roundToInt(tree.getRawParameterValue(params::kQuality)->load());
        eco_.setOn(quality == 0);
        high_.setOn(quality == 1);
        const auto stereo = juce::roundToInt(tree.getRawParameterValue(params::kStereo)->load());
        linked_.setOn(stereo == 0);
        stereo_.setOn(stereo == 1);
        mid_side_.setOn(stereo == 2);
        removed_.setOn(p_ref_.getListen() == dsp::Listen::kRemoved);
        output_.setValue(tree.getRawParameterValue(params::kOutput)->load());
        bypass_.setOn(tree.getRawParameterValue(params::kBypass)->load() > .5f);

        const auto show_stereo = p_ref_.getMainBusNumOutputChannels() > 1;
        if (show_stereo != show_stereo_) {
            show_stereo_ = show_stereo;
            linked_.setVisible(show_stereo);
            stereo_.setVisible(show_stereo);
            mid_side_.setVisible(show_stereo);
            repaint();
        }
        const auto rate = p_ref_.getSampleRate();
        const auto text = rate > 0.0 ? juce::String(1000.0 * p_ref_.getLatencySamples() / rate, 1) + " ms" : juce::String("--");
        if (text != latency_text_) {
            latency_text_ = text;
            repaint(latency_area_.toNearestInt());
        }
    }

    void Footer::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, 6.f);
        auto label = [&](juce::Rectangle<float>& area, const juce::String& text) {
            const auto w = juce::GlyphArrangement::getStringWidth(captionFont(), text) + size::kS2;
            area = b.removeFromLeft(w);
        };
        label(quality_label_, "QUALITY");
        eco_.setBounds(b.removeFromLeft(static_cast<float>(eco_.getIdealWidth())).toNearestInt());
        b.removeFromLeft(2.f);
        high_.setBounds(b.removeFromLeft(static_cast<float>(high_.getIdealWidth())).toNearestInt());
        b.removeFromLeft(size::kS5);
        label(stereo_label_, "STEREO");
        linked_.setBounds(b.removeFromLeft(static_cast<float>(linked_.getIdealWidth())).toNearestInt());
        b.removeFromLeft(2.f);
        stereo_.setBounds(b.removeFromLeft(static_cast<float>(stereo_.getIdealWidth())).toNearestInt());
        b.removeFromLeft(2.f);
        mid_side_.setBounds(b.removeFromLeft(static_cast<float>(mid_side_.getIdealWidth())).toNearestInt());
        b.removeFromLeft(size::kS5);
        removed_.setBounds(b.removeFromLeft(static_cast<float>(removed_.getIdealWidth())).toNearestInt());

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

        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("QUALITY", quality_label_, juce::Justification::centredLeft);
        if (show_stereo_) {
            g.drawText("STEREO", stereo_label_, juce::Justification::centredLeft);
        }
        // a quiet separator before the audition
        const auto x = static_cast<float>(removed_.getX()) - size::kS5 * .5f;
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(x, 10.f, 1.f, b.getHeight() - 20.f));

        auto latency = latency_area_;
        const auto value = latency.removeFromRight(Fonts::get().getTabularWidth(latency_text_, type::kBody));
        latency.removeFromRight(size::kS2);
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("LATENCY", latency, juce::Justification::centredRight);
        g.setColour(colour::kTextSecondary);
        Fonts::get().drawTabular(g, latency_text_, value, juce::Justification::centredRight, type::kBody);
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
        if (meter_.tick(dt, peaks[0], p_ref_.getMainBusNumOutputChannels() > 1 ? peaks[1] : peaks[0])) {
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
