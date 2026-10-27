// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "chrome.hpp"

#include <altvane_ui/theme/fonts.hpp>
#include <altvane_ui/util/value_format.hpp>

#include "theme/deesser_tokens.hpp"

namespace altvane::deesser {
    using namespace tokens;
    using ui::Fonts;

    // ---------------------------------------------------------------- Footer

    Footer::Footer(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        static constexpr std::array<const char*, 3> kStereoTips{
            "Stereo: de-ess both channels (Link sets how much they share)",
            "Mid: de-ess only the centre of the image",
            "Side: de-ess only the sides of the image"
        };
        for (size_t i = 0; i < stereo_.size(); ++i) {
            auto& chip = stereo_[i];
            chip.setTooltip(kStereoTips[i]);
            chip.setComponentID(params::kStereoMode);
            chip.on_click = [this, i] { setChoice(params::kStereoMode, static_cast<int>(i), "Stereo Mode"); };
            addAndMakeVisible(chip);
        }
        side_chain_[0].setTooltip("Detect sibilance in the track itself");
        side_chain_[1].setTooltip("Detect sibilance in the side-chain input (route one to the plug-in in your host)");
        for (size_t i = 0; i < side_chain_.size(); ++i) {
            auto& chip = side_chain_[i];
            chip.setComponentID(params::kSideChain);
            chip.on_click = [this, i] { setChoice(params::kSideChain, static_cast<int>(i), "Side Chain"); };
            addAndMakeVisible(chip);
        }

        ui::bindValue(link_, editor_, p.parameters_.getParameter(params::kStereoLink), "Link");
        link_.format = [](const float v) { return ui::format::percent(v); };
        ui::bindValue(lookahead_, editor_, p.parameters_.getParameter(params::kLookahead), "Lookahead");
        lookahead_.format = [](const float v) { return ui::format::ms(v); };
        ui::bindValue(output_, editor_, p.parameters_.getParameter(params::kOutput), "Output");
        output_.format = [](const float v) { return ui::format::gain(v); };
        for (auto* c : std::initializer_list<juce::Component*>{&link_, &lookahead_, &output_, &bypass_}) {
            addAndMakeVisible(c);
        }

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
        const auto stereo = juce::roundToInt(tree.getRawParameterValue(params::kStereoMode)->load());
        for (size_t i = 0; i < stereo_.size(); ++i) {
            stereo_[i].setOn(stereo == static_cast<int>(i));
        }
        // link only means something with two channels de-essed side by side
        link_.setEnabledLook(stereo == 0);
        const auto side_chain = juce::roundToInt(tree.getRawParameterValue(params::kSideChain)->load());
        side_chain_[0].setOn(side_chain == 0);
        side_chain_[1].setOn(side_chain == 1);
        link_.setValue(tree.getRawParameterValue(params::kStereoLink)->load());
        lookahead_.setValue(tree.getRawParameterValue(params::kLookahead)->load());
        output_.setValue(tree.getRawParameterValue(params::kOutput)->load());
        bypass_.setOn(tree.getRawParameterValue(params::kBypass)->load() > .5f);

        const auto show_stereo = p_ref_.getTotalNumOutputChannels() > 1;
        if (show_stereo != show_stereo_) {
            show_stereo_ = show_stereo;
            for (auto& chip : stereo_) {
                chip.setVisible(show_stereo);
            }
            link_.setVisible(show_stereo);
            resized();
            repaint();
        }
        // Ext asks for a side chain the host has to provide: say so when there is none
        const auto have_side_chain = p_ref_.hasSideChainInput();
        if (have_side_chain != have_side_chain_) {
            have_side_chain_ = have_side_chain;
            side_chain_[1].setTooltip(have_side_chain
                                          ? "Detect sibilance in the side-chain input"
                                          : "Detect sibilance in the side-chain input (none is connected: route one to the plug-in in your host)");
        }
        side_chain_[1].setAlpha(have_side_chain || side_chain == 1 ? 1.f : .5f);
    }

    void Footer::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, 6.f);
        auto chip = [&](ui::Chip& c) {
            c.setBounds(b.removeFromLeft(static_cast<float>(c.getIdealWidth())).toNearestInt());
            b.removeFromLeft(2.f);
        };
        if (show_stereo_) {
            for (auto& c : stereo_) {
                chip(c);
            }
            b.removeFromLeft(size::kS1);
            link_.setBounds(b.removeFromLeft(96.f).toNearestInt());
            b.removeFromLeft(size::kS3);
            separators_[0] = b.getX();
            b.removeFromLeft(size::kS3);
        } else {
            separators_[0] = -1.f;
        }
        side_chain_label_ = b.removeFromLeft(juce::GlyphArrangement::getStringWidth(captionFont(), "SIDE CHAIN") + size::kS2);
        chip(side_chain_[0]);
        chip(side_chain_[1]);
        b.removeFromLeft(size::kS3);
        separators_[1] = b.getX();
        b.removeFromLeft(size::kS3);
        lookahead_.setBounds(b.removeFromLeft(144.f).toNearestInt());

        b.removeFromRight(10.f); // window resize grip
        bypass_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(size::kS2);
        output_.setBounds(b.removeFromRight(124.f).toNearestInt());
    }

    void Footer::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, b.getWidth(), 1.f));
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("SIDE CHAIN", side_chain_label_, juce::Justification::centredLeft);
        g.setColour(colour::kHairline);
        for (const auto x : separators_) {
            if (x >= 0.f) {
                g.fillRect(juce::Rectangle<float>(x, 10.f, 1.f, b.getHeight() - 20.f));
            }
        }
    }

    // ---------------------------------------------------------------- MeterRail

    MeterRail::MeterRail(PluginProcessor& p) : p_ref_(p) {
        setTitle("Gain reduction and output meters");
    }

    void MeterRail::resized() {
        auto b = getLocalBounds().toFloat().reduced(6.f, 8.f);
        const auto half = b.getWidth() * .5f;
        auto left = b.removeFromLeft(half), right = b;
        auto column = [](juce::Rectangle<float> c, juce::Rectangle<float>& label, juce::Rectangle<float>& meter,
                         juce::Rectangle<float>& readout, const float width) {
            label = c.removeFromTop(16.f);
            c.removeFromTop(8.f);
            readout = c.removeFromBottom(16.f);
            c.removeFromBottom(6.f);
            meter = c.withSizeKeepingCentre(width, c.getHeight());
        };
        column(left, gr_label_, gr_meter_, gr_readout_, 8.f);
        column(right, out_label_, out_meter_, out_readout_, 14.f);
    }

    void MeterRail::tick(const double dt, const float reduction) {
        bool dirty = false;
        // gain reduction: instant attack, a quick fall, the largest held for two seconds
        const auto fallen = std::max(0.f, reduction_ - 30.f * static_cast<float>(dt));
        const auto next = std::max(reduction, fallen);
        if (std::abs(next - reduction_) > .01f) {
            reduction_ = next;
            dirty = true;
        }
        hold_age_ += dt;
        if (reduction > reduction_hold_ || hold_age_ > 2.0) {
            if (std::abs(reduction - reduction_hold_) > .05f) {
                dirty = true;
            }
            reduction_hold_ = reduction;
            hold_age_ = 0.0;
        }
        const auto peaks = p_ref_.takeOutputPeaks();
        dirty = output_.tick(dt, peaks[0], p_ref_.getTotalNumOutputChannels() > 1 ? peaks[1] : peaks[0]) || dirty;
        if (dirty) {
            repaint();
        }
    }

    void MeterRail::paintReduction(juce::Graphics& g) const {
        const auto m = gr_meter_;
        g.setColour(colour::kControl);
        g.fillRoundedRectangle(m, 2.f);
        // ticks every 6 dB
        for (float db = 6.f; db < kReductionScaleDb; db += 6.f) {
            const auto y = m.getY() + m.getHeight() * db / kReductionScaleDb;
            g.setColour(colour::kHairlineStrong);
            g.fillRect(juce::Rectangle<float>(m.getRight() + 2.f, y, 3.f, 1.f));
        }
        if (reduction_ > .05f) {
            const auto h = m.getHeight() * std::min(reduction_, kReductionScaleDb) / kReductionScaleDb;
            const auto bar = m.withHeight(h);
            g.setGradientFill(juce::ColourGradient(colour::kReduction.withAlpha(.75f), 0.f, m.getY(),
                                                   colour::kReduction, 0.f, m.getY() + h, false));
            g.fillRoundedRectangle(bar, 2.f);
        }
        const auto text = reduction_hold_ > .05f ? ui::format::kMinus + juce::String(reduction_hold_, 1) : juce::String("0.0");
        g.setColour(reduction_hold_ > .05f ? colour::kReduction : colour::kTextTertiary);
        Fonts::get().drawTabular(g, text, gr_readout_.expanded(6.f, 0.f), juce::Justification::centred, type::kSmall);
    }

    void MeterRail::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, 1.f, b.getHeight()));
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("GR", gr_label_, juce::Justification::centred);
        g.drawText("OUT", out_label_, juce::Justification::centred);
        paintReduction(g);
        output_.paint(g, out_meter_, out_readout_);
    }

    void MeterRail::mouseUp(const juce::MouseEvent& e) {
        if (gr_meter_.expanded(8.f, 0.f).contains(e.position) || gr_readout_.contains(e.position)) {
            reduction_hold_ = 0.f;
            repaint();
        }
        if (out_meter_.expanded(8.f, 0.f).contains(e.position) || out_readout_.contains(e.position)) {
            output_.resetPeak();
            repaint();
        }
    }
}
