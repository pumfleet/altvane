// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "advanced_popover.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/widgets/help_balloon.hpp"
#include "../model/formats.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;

    AdvancedPopover::AdvancedPopover(CompBridge& bridge) : bridge_(bridge) {
        setTitle("Advanced");
        setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        setWantsKeyboardFocus(true);

        const auto slider = [this](ui::Slider& s, const char* id, const juce::String& name,
                                   std::function<juce::String(float)> format, const juce::String& help) {
            ui::bindValue(s, bridge_.getEditor(), bridge_.para(id), name);
            s.format = std::move(format);
            ui::setHelp(s, name, help);
            addAndMakeVisible(s);
        };
        slider(rms_mix_, zlp::PRMSMix::kID, "RMS mix", formats::percent, "Blend between peak and RMS detection.");
        slider(rms_length_, zlp::PRMSLength::kID, "Length", formats::ms, "The RMS averaging window.");
        slider(rms_speed_, zlp::PRMSSpeed::kID, "Speed",
               [](const float v) { return juce::String(v, 2) + juce::String::fromUTF8("\xc3\x97"); },
               "Scales attack and release for the RMS detector.");
        slider(pump_, zlp::PPump::kID, "Pump", formats::percent,
               "Limits how fast the gain may swing back, for an audible breathing release.");
        slider(smooth_, zlp::PSmooth::kID, "Smooth", formats::percent,
               "Rounds off the envelope so fast transients cause less distortion.");
        // shown as -100 .. +100 %: 0 is the engine's straight slope
        slider(curve_, zlp::PCurve::kID, "Curve", [](const float v) {
            const auto shown = juce::roundToInt(zlp::PCurve::formatV(v) * 100.f);
            return (shown > 0 ? "+" : "") + formats::number(static_cast<float>(shown), 0) + "%";
        }, "Bends the curve above the threshold: positive gets harder towards 0 dB, negative softer.");
        curve_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = ui::format::parseNumber(t)) {
                return static_cast<float>((*v + 100.0) * .5);
            }
            return std::nullopt;
        };
        curve_.setBipolar(true);
        slider(floor_, zlp::PFloor::kID, "Floor", formats::db, "Below this level the expander or inflator stops.");
        slider(clipper_, zlp::PClipperDrive::kID, "Clipper", formats::percent,
               "A soft clipper after the compressor that catches what the attack lets through.");
        rms_length_.parse = formats::parseMs;
        addChildComponent(floor_);

        rms_.setTitle("RMS detection");
        rms_.on_change = [this](bool) { bridge_.toggle(zlp::PRMSON::kID, "RMS Detection"); };
        addAndMakeVisible(rms_);
        tick();
    }

    void AdvancedPopover::tick() {
        const auto rms_on = bridge_.getBool(zlp::PRMSON::kID);
        rms_.setOn(rms_on);
        for (auto* s : {&rms_mix_, &rms_length_, &rms_speed_}) {
            s->setEnabledLook(rms_on);
        }
        rms_mix_.setValue(bridge_.get(zlp::PRMSMix::kID));
        rms_length_.setValue(bridge_.get(zlp::PRMSLength::kID));
        rms_speed_.setValue(bridge_.get(zlp::PRMSSpeed::kID));
        pump_.setValue(bridge_.get(zlp::PPump::kID));
        smooth_.setValue(bridge_.get(zlp::PSmooth::kID));
        curve_.setValue(bridge_.get(zlp::PCurve::kID));
        floor_.setValue(bridge_.get(zlp::PFloor::kID));
        clipper_.setValue(bridge_.get(zlp::PClipperDrive::kID));
        // the curve shapes compression; inflating and expanding stop at the floor instead
        const auto below = bridge_.getDirection() == zlp::PCompDirection::kInflate
                           || bridge_.getDirection() == zlp::PCompDirection::kExpand;
        curve_.setVisible(!below);
        floor_.setVisible(below);
    }

    void AdvancedPopover::resized() {
        auto b = getLocalBounds().reduced(14, 12);
        const auto column = [&](const size_t i) {
            auto col = b.removeFromLeft(140);
            b.removeFromLeft(18);
            headers_[i] = col.removeFromTop(18).toFloat();
            col.removeFromTop(4);
            return col;
        };
        auto det = column(0);
        rms_.setBounds(headers_[0].removeFromRight(30.f).withSizeKeepingCentre(30.f, 18.f).toNearestInt());
        for (auto* s : {&rms_mix_, &rms_length_, &rms_speed_}) {
            s->setBounds(det.removeFromTop(size::kSliderHeight));
            det.removeFromTop(4);
        }
        auto env = column(1);
        pump_.setBounds(env.removeFromTop(size::kSliderHeight));
        env.removeFromTop(4);
        smooth_.setBounds(env.removeFromTop(size::kSliderHeight));
        env.removeFromTop(4);
        const auto last = env.removeFromTop(size::kSliderHeight);
        curve_.setBounds(last);
        floor_.setBounds(last);
        auto out = column(2);
        clipper_.setBounds(out.removeFromTop(size::kSliderHeight));
    }

    void AdvancedPopover::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colours::black.withAlpha(.35f));
        g.fillRoundedRectangle(b.translated(0.f, 4.f), size::kRadiusLarge);
        g.setColour(colour::kOverlay);
        g.fillRoundedRectangle(b, size::kRadiusLarge);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(b.reduced(.5f), size::kRadiusLarge, 1.f);

        g.setFont(Fonts::get().font(type::kCaption, Fonts::kSemiBold).withExtraKerningFactor(.1f));
        g.setColour(colour::kTextSecondary);
        const std::array<const char*, 3> names{"DETECTION", "ENVELOPE", "OUTPUT"};
        for (size_t i = 0; i < 3; ++i) {
            g.drawText(names[i], headers_[i], juce::Justification::centredLeft);
        }
    }

    bool AdvancedPopover::keyPressed(const juce::KeyPress& key) {
        if (key == juce::KeyPress::escapeKey) {
            if (on_close) {
                on_close();
            }
            return true;
        }
        return false;
    }
}
