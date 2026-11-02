// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "footer.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/widgets/help_balloon.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;

    namespace {
        juce::Font captionFont() {
            return Fonts::get().font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f);
        }

        // the scales offered, as indices of the display's lowest level (zlstate::PAnalyzerMinDB)
        constexpr std::array kScaleIndices{1, 2, 3, 4, 5};

        juce::String oversampleName(const int index) {
            return index == 0 ? juce::String("Off") : juce::String(1 << index) + juce::String::fromUTF8("\xc3\x97");
        }
    }

    Footer::Footer(CompBridge& bridge) : bridge_(bridge) {
        oversample_.setComponentID(zlp::POversample::kID);
        oversample_.setTitle("Oversampling");
        ui::setHelp(oversample_, "Oversampling", "Runs the compressor at a higher rate to reduce aliasing from fast "
                    "gain changes. Adds latency.");
        oversample_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = bridge_.getChoice(zlp::POversample::kID);
            for (int i = 0; i < zlp::POversample::kChoices.size(); ++i) {
                menu.addItem(i + 1, oversampleName(i), true, i == current);
            }
            return menu;
        };
        oversample_.on_result = [this](const int id) {
            bridge_.set(zlp::POversample::kID, static_cast<float>(id - 1), "Oversampling");
        };

        delta_.setComponentID(zlp::PCompDelta::kID);
        delta_.setOnColour(colour::kWarning);
        ui::setHelp(delta_, "Delta", "Hear only what the compressor changes.");
        delta_.on_click = [this] { bridge_.toggle(zlp::PCompDelta::kID, "Delta"); };

        time_.setTitle("Display time");
        ui::setHelp(time_, "Display time", "How much history the level display shows.");
        time_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = bridge_.getDisplayChoice(zlstate::PAnalyzerTimeLength::kID);
            for (int i = 0; i < zlstate::PAnalyzerTimeLength::kChoices.size(); ++i) {
                menu.addItem(i + 1, zlstate::PAnalyzerTimeLength::kChoices[i], true, i == current);
            }
            return menu;
        };
        time_.on_result = [this](const int id) {
            bridge_.setDisplay(zlstate::PAnalyzerTimeLength::kID, static_cast<float>(id - 1));
        };

        scale_.setTitle("Scale");
        ui::setHelp(scale_, "Scale", "The dB range of the level display, the knee display and the meters.");
        scale_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = bridge_.getDisplayChoice(zlstate::PAnalyzerMinDB::kID);
            for (const auto i : kScaleIndices) {
                const auto db = -zlstate::PAnalyzerMinDB::kDBs[static_cast<size_t>(i)];
                menu.addItem(i + 1, juce::String(juce::roundToInt(db)) + " dB", true, i == current);
            }
            return menu;
        };
        scale_.on_result = [this](const int id) {
            bridge_.setDisplay(zlstate::PAnalyzerMinDB::kID, static_cast<float>(id - 1));
        };

        ui::setHelp(peak_, "Peak", "Show peak levels.");
        ui::setHelp(rms_, "RMS", "Show RMS levels.");
        peak_.on_click = [this] { bridge_.setDisplay(zlstate::PAnalyzerMagType::kID, 0.f); };
        rms_.on_click = [this] { bridge_.setDisplay(zlstate::PAnalyzerMagType::kID, 1.f); };

        bypass_.setComponentID(zlp::PCompON::kID);
        bypass_.setToggleable(true);
        ui::setHelp(bypass_, "Bypass", "Turns the compressor off, keeping its latency so nothing jumps.");
        bypass_.setOnColour(colour::kDanger);
        bypass_.on_click = [this] { bridge_.toggle(zlp::PCompON::kID, "Bypass"); };

        for (auto* c : std::initializer_list<juce::Component*>{
                 &oversample_, &delta_, &time_, &scale_, &peak_, &rms_, &bypass_
             }) {
            addAndMakeVisible(c);
        }
        tick();
    }

    void Footer::tick() {
        oversample_.setText(oversampleName(bridge_.getChoice(zlp::POversample::kID)));
        delta_.setOn(bridge_.getBool(zlp::PCompDelta::kID));
        time_.setText(zlstate::PAnalyzerTimeLength::kChoices[bridge_.getDisplayChoice(
            zlstate::PAnalyzerTimeLength::kID)]);
        scale_.setText(juce::String(juce::roundToInt(bridge_.getScaleDB())) + " dB");
        const auto rms = bridge_.getDisplayChoice(zlstate::PAnalyzerMagType::kID) == 1;
        peak_.setOn(!rms);
        rms_.setOn(rms);
        bypass_.setOn(!bridge_.getBool(zlp::PCompON::kID));

        const auto& p = bridge_.getProcessor();
        const auto rate = p.getSampleRate();
        const auto text = rate > 0.0 ? juce::String(1000.0 * p.getLatencySamples() / rate, 1) + " ms" : juce::String("--");
        if (text != latency_text_) {
            latency_text_ = text;
            repaint(latency_area_.toNearestInt());
        }
    }

    void Footer::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, 6.f);
        const auto label = [&](juce::Rectangle<float>& area, const juce::String& text) {
            area = b.removeFromLeft(juce::GlyphArrangement::getStringWidth(captionFont(), text) + size::kS2);
        };
        label(oversample_label_, "OVERSAMPLING");
        oversample_.setBounds(b.removeFromLeft(64.f).toNearestInt());
        b.removeFromLeft(size::kS1);
        delta_.setBounds(b.removeFromLeft(static_cast<float>(delta_.getIdealWidth())).toNearestInt());
        b.removeFromLeft(size::kS4);
        separator_x_ = b.getX() - size::kS4 * .5f;
        b.removeFromLeft(size::kS1);
        label(display_label_, "DISPLAY");
        time_.setBounds(b.removeFromLeft(64.f).toNearestInt());
        b.removeFromLeft(size::kS1);
        scale_.setBounds(b.removeFromLeft(72.f).toNearestInt());
        b.removeFromLeft(size::kS2);
        peak_.setBounds(b.removeFromLeft(static_cast<float>(peak_.getIdealWidth())).toNearestInt());
        b.removeFromLeft(2.f);
        rms_.setBounds(b.removeFromLeft(static_cast<float>(rms_.getIdealWidth())).toNearestInt());

        b.removeFromRight(10.f); // window resize grip
        bypass_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(size::kS4);
        latency_area_ = b.removeFromRight(120.f);
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
        g.drawText("OVERSAMPLING", oversample_label_, juce::Justification::centredLeft);
        g.drawText("DISPLAY", display_label_, juce::Justification::centredLeft);

        auto latency = latency_area_;
        const auto value = latency.removeFromRight(Fonts::get().getTabularWidth(latency_text_, type::kBody));
        latency.removeFromRight(size::kS2);
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("LATENCY", latency, juce::Justification::centredRight);
        g.setColour(colour::kTextSecondary);
        Fonts::get().drawTabular(g, latency_text_, value, juce::Justification::centredRight, type::kBody);
    }
}
