// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "footer.hpp"

#include <altvane_ui/widgets/help_balloon.hpp>

namespace altvane::limiter {
    using namespace tokens;
    using ui::Fonts;

    Footer::Footer(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &oversampling_, &dither_, &shaped_, &dc_, &audition_, &history_, &range_, &scale_, &bypass_}) {
            addAndMakeVisible(c);
        }
        auto& tree = p.parameters_;
        auto& na = p.na_parameters_;
        namespace params = altvane::limiter::params;

        setupChoice(oversampling_, tree, zlp::POversampling::kID, "Oversampling", true,
                    {"Oversampling off", "Oversampling 2x", "Oversampling 4x", "Oversampling 8x",
                     "Oversampling 16x", "Oversampling 32x"});
        oversampling_.setLabel("Oversampling");
        ui::setHelp(oversampling_, "Oversampling", "Runs the limiter at a higher rate, for cleaner limiting of bright "
                                                   "material. Costs CPU and adds latency.");
        setupChoice(dither_, tree, params::PDitherBits::kID, "Dither", true,
                    {"No dither", "Dither 24 bit", "Dither 20 bit", "Dither 16 bit"});
        dither_.setLabel("Dither");
        ui::setHelp(dither_, "Dither", "Reduce the output to 16, 20 or 24 bits with dither. Use it only on the last "
                                       "plug-in before export, at the file's bit depth.");

        shaped_.setComponentID(params::PDitherShape::kID);
        shaped_.on_click = [this] {
            const auto shaped = index(p_ref_.parameters_, params::PDitherShape::kID) > 0;
            editor_.setNow({{p_ref_.parameters_.getParameter(params::PDitherShape::kID), shaped ? 0.f : 1.f}},
                           "Noise Shaping");
        };
        ui::setHelp(shaped_, "Noise shaping", "Moves the dither noise out of the frequencies hearing is most "
                                              "sensitive to, into the top octave (44.1 kHz and up).");
        dc_.setComponentID(params::PDCFilter::kID);
        dc_.on_click = [this] { editor_.toggle(p_ref_.parameters_.getParameter(params::PDCFilter::kID), "DC Filter"); };
        ui::setHelp(dc_, "DC filter", "Removes DC offset before the limiter, so it does not use up headroom.");

        audition_.setOnColour(colour::kWarning);
        audition_.setComponentID(zlp::PDelta::kID);
        audition_.on_click = [this] { editor_.toggle(p_ref_.parameters_.getParameter(zlp::PDelta::kID), "Audition"); };
        ui::setHelp(audition_, "Audition", "Hear only what the limiter removes. Distortion and pumping are easy to "
                                           "spot here.");

        setupChoice(history_, na, zlstate::PAnalyzerTimeLength::kID, "History", false,
                    {"6 s", "9 s", "12 s", "18 s"});
        ui::setHelp(history_, "History", "How many seconds the display shows.");
        setupChoice(range_, na, zlstate::PAnalyzerMinDB::kID, "Range", false,
                    {"6 dB", "12 dB", "24 dB", "36 dB", "54 dB"});
        ui::setHelp(range_, "Range", "How far down the display reaches.");
        setupChoice(scale_, na, zlstate::PLoudnessScale::kID, "Loudness scale", false,
                    {"LUFS", "LU (target)", "K-12", "K-14", "K-20"});
        ui::setHelp(scale_, "Loudness scale", "How the loudness meter is labelled: absolute LUFS, LU from the target, "
                                              "or a K-system scale.");

        bypass_.setToggleable(true);
        bypass_.setComponentID(zlp::PBypass::kID);
        bypass_.setOnColour(colour::kWarning);
        bypass_.on_click = [this] { editor_.toggle(p_ref_.parameters_.getParameter(zlp::PBypass::kID), "Bypass"); };
        ui::setHelp(bypass_, "Bypass", "Pass the input through, delayed by the latency so it stays in time.");
        tick();
    }

    void Footer::setupChoice(ui::Selector& selector, juce::AudioProcessorValueTreeState& tree, const char* id,
                             const juce::String& name, const bool undoable, const juce::StringArray& labels) {
        auto* para = dynamic_cast<juce::AudioParameterChoice*>(tree.getParameter(id));
        jassert(para != nullptr);
        selector.setComponentID(undoable ? juce::String(id) : juce::String());
        selector.build_menu = [para, labels] {
            juce::PopupMenu menu;
            for (int i = 0; i < para->choices.size(); ++i) {
                menu.addItem(i + 1, labels.isEmpty() ? para->choices[i] : labels[i], true, para->getIndex() == i);
            }
            return menu;
        };
        selector.on_result = [this, para, name, undoable](const int result) {
            if (result <= 0) {
                return;
            }
            const auto plain = static_cast<float>(result - 1);
            if (undoable) {
                editor_.setNow({{para, plain}}, "Change " + name);
            } else {
                ui::ParamEditor::setNotifyingHost(para, para->convertTo0to1(plain));
            }
        };
        selector.getProperties().set("labels", labels.joinIntoString("|"));
    }

    void Footer::tick() {
        auto& tree = p_ref_.parameters_;
        auto& na = p_ref_.na_parameters_;
        namespace params = altvane::limiter::params;
        const auto text = [](ui::Selector& s, juce::AudioProcessorValueTreeState& t, const char* id) {
            auto* para = dynamic_cast<juce::AudioParameterChoice*>(t.getParameter(id));
            const auto labels = juce::StringArray::fromTokens(s.getProperties()["labels"].toString(), "|", "");
            const auto i = para->getIndex();
            s.setText(labels.size() > i && labels[i].isNotEmpty() ? labels[i] : para->choices[i]);
        };
        text(oversampling_, tree, zlp::POversampling::kID);
        text(dither_, tree, params::PDitherBits::kID);
        text(history_, na, zlstate::PAnalyzerTimeLength::kID);
        text(range_, na, zlstate::PAnalyzerMinDB::kID);
        text(scale_, na, zlstate::PLoudnessScale::kID);
        const auto dither_on = index(tree, params::PDitherBits::kID) > 0;
        shaped_.setOn(index(tree, params::PDitherShape::kID) > 0);
        shaped_.setAlpha(dither_on ? 1.f : .4f);
        dc_.setOn(value(tree, params::PDCFilter::kID) > .5f);
        audition_.setOn(value(tree, zlp::PDelta::kID) > .5f);
        bypass_.setOn(value(tree, zlp::PBypass::kID) > .5f);

        const auto rate = p_ref_.getSampleRate();
        const auto latency = rate > 0.0 ? juce::String(1000.0 * p_ref_.getLatencySamples() / rate, 2) + " ms"
                                        : juce::String("--");
        if (latency != latency_text_) {
            latency_text_ = latency;
            repaint(latency_area_.toNearestInt());
        }
    }

    void Footer::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, 6.f);
        const auto place = [&](juce::Component& c, const float w) {
            c.setBounds(b.removeFromLeft(w).toNearestInt());
            b.removeFromLeft(4.f);
        };
        place(oversampling_, 128.f);
        b.removeFromLeft(size::kS2);
        place(dither_, 108.f);
        place(shaped_, static_cast<float>(shaped_.getIdealWidth()));
        place(dc_, static_cast<float>(dc_.getIdealWidth()));
        b.removeFromLeft(size::kS3);
        separator_x_ = b.getX() - size::kS3 * .5f;
        place(audition_, static_cast<float>(audition_.getIdealWidth()));

        b.removeFromRight(10.f); // window resize grip
        bypass_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(size::kS3);
        latency_area_ = b.removeFromRight(110.f);
        b.removeFromRight(size::kS3);
        scale_.setBounds(b.removeFromRight(96.f).toNearestInt());
        b.removeFromRight(4.f);
        range_.setBounds(b.removeFromRight(70.f).toNearestInt());
        b.removeFromRight(4.f);
        history_.setBounds(b.removeFromRight(62.f).toNearestInt());
        const auto w = juce::GlyphArrangement::getStringWidth(captionFont(), "DISPLAY") + size::kS2;
        display_label_ = b.removeFromRight(w);
    }

    void Footer::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, b.getWidth(), 1.f));
        g.fillRect(juce::Rectangle<float>(separator_x_, 10.f, 1.f, b.getHeight() - 20.f));

        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("DISPLAY", display_label_, juce::Justification::centredLeft);

        auto latency = latency_area_;
        const auto value_area = latency.removeFromRight(Fonts::get().getTabularWidth(latency_text_, type::kBody));
        latency.removeFromRight(size::kS2);
        g.setColour(colour::kTextTertiary);
        g.drawText("LATENCY", latency, juce::Justification::centredRight);
        g.setColour(colour::kTextSecondary);
        Fonts::get().drawTabular(g, latency_text_, value_area, juce::Justification::centredRight, type::kBody);
    }
}
