// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "side_chain_panel.hpp"

#include "altvane_ui/widgets/help_balloon.hpp"
#include "../model/formats.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;

    namespace {
        constexpr std::array kStereoNames{"Mid / Side", "Left / Right", "Mid / Side max", "Left / Right max"};
        // holding Listen longer than this makes it momentary
        constexpr juce::uint32 kHoldMs = 300;
    }

    SideChainPanel::SideChainPanel(CompBridge& bridge, SideSpectrum& spectrum) :
        bridge_(bridge), eq_(bridge, spectrum) {
        setOpaque(true);
        setTitle("Side chain");
        setFocusContainerType(FocusContainerType::focusContainer);

        ui::setHelp(internal_, "Internal", "The detector listens to the track itself.");
        ui::setHelp(external_, "External", "The detector listens to the side-chain input.");
        internal_.setComponentID(zlp::PExtSide::kID);
        external_.setComponentID(zlp::PExtSide::kID);
        internal_.on_click = [this] { bridge_.set(zlp::PExtSide::kID, 0.f, "Side-Chain Source"); };
        external_.on_click = [this] { bridge_.set(zlp::PExtSide::kID, 1.f, "Side-Chain Source"); };

        listen_.setComponentID(zlp::PSideOut::kID);
        listen_.setOnColour(colour::kWarning);
        ui::setHelp(listen_, "Listen", "Hear the side-chain signal. Click to latch, hold to listen only while pressed.");
        listen_.addMouseListener(this, false);
        // the keyboard and accessibility clients toggle; the mouse is handled in mouseDown / mouseUp
        listen_.on_click = [this] {
            if (!listen_pressed_) {
                bridge_.toggle(zlp::PSideOut::kID, "Side-Chain Listen");
            }
        };

        stereo_.setComponentID(zlp::PSideStereoMode::kID);
        stereo_.setTitle("Stereo");
        ui::setHelp(stereo_, "Stereo",
                    "Detect on mid / side or left / right. The max modes let the louder channel drive both.");
        stereo_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = bridge_.getChoice(zlp::PSideStereoMode::kID);
            for (int i = 0; i < 4; ++i) {
                menu.addItem(i + 1, kStereoNames[static_cast<size_t>(i)], true, i == current);
            }
            return menu;
        };
        stereo_.on_result = [this](const int id) {
            bridge_.set(zlp::PSideStereoMode::kID, static_cast<float>(id - 1), "Side-Chain Stereo");
        };

        swap_.setComponentID(zlp::PSideStereoSwap::kID);
        ui::setHelp(swap_, "Swap", "Swap the two side-chain channels (mid and side, or left and right).");
        swap_.on_click = [this] { bridge_.toggle(zlp::PSideStereoSwap::kID, "Side-Chain Swap"); };

        eq_on_.setComponentID(zlp::PSideEQBypass::kID);
        ui::setHelp(eq_on_, "Side-chain EQ", "Turn the whole side-chain EQ on or off without losing its bands.");
        eq_on_.on_click = [this] { bridge_.toggle(zlp::PSideEQBypass::kID, "Side-Chain EQ"); };

        const auto knob = [this](ui::Knob& k, const char* id, const juce::String& name,
                                 std::function<juce::String(float)> format, const juce::String& help) {
            ui::bindValue(k, bridge_.getEditor(), bridge_.para(id), name);
            k.format = std::move(format);
            ui::setHelp(k, name, help);
            addAndMakeVisible(k);
        };
        knob(gain_, zlp::PSideGain::kID, "Gain", formats::signedDB, "Level of the side chain going into the detector.");
        knob(link_, zlp::PSideStereoLink::kID, "Link", formats::percent,
             "0% detects each channel on its own; 100% applies the same gain change to both.");
        knob(amount1_, zlp::PSideStereoWet1::kID, "Mid", formats::percent,
             "How much of the gain change reaches this channel");
        knob(amount2_, zlp::PSideStereoWet2::kID, "Side", formats::percent,
             "How much of the gain change reaches this channel");
        gain_.setBipolar(true);
        // the amounts are labelled by the stereo mode
        ui::setHelp(amount1_, "Mid or left amount", "How much of the gain change reaches the mid, or the left channel.");
        ui::setHelp(amount2_, "Side or right amount",
                    "How much of the gain change reaches the side, or the right channel.");

        for (auto* c : std::initializer_list<juce::Component*>{
                 &internal_, &external_, &listen_, &stereo_, &swap_, &eq_on_, &eq_
             }) {
            addAndMakeVisible(c);
        }
        eq_on_.toFront(false);
        tick();
    }

    void SideChainPanel::tick() {
        const auto external = bridge_.getBool(zlp::PExtSide::kID);
        internal_.setOn(!external);
        external_.setOn(external);
        listen_.setOn(bridge_.getBool(zlp::PSideOut::kID));
        stereo_.setText(kStereoNames[static_cast<size_t>(std::clamp(bridge_.getChoice(zlp::PSideStereoMode::kID), 0, 3))]);
        swap_.setOn(bridge_.getChoice(zlp::PSideStereoSwap::kID) != 0);
        eq_on_.setOn(!bridge_.getBool(zlp::PSideEQBypass::kID));
        gain_.setValue(bridge_.get(zlp::PSideGain::kID));
        link_.setValue(bridge_.get(zlp::PSideStereoLink::kID));
        amount1_.setValue(bridge_.get(zlp::PSideStereoWet1::kID));
        amount2_.setValue(bridge_.get(zlp::PSideStereoWet2::kID));
        const auto ms = bridge_.isMidSide();
        amount1_.setLabel(ms ? "Mid" : "Left");
        amount2_.setLabel(ms ? "Side" : "Right");
        eq_.tick();
    }

    void SideChainPanel::mouseDown(const juce::MouseEvent& e) {
        if (e.eventComponent == &listen_) {
            listen_pressed_ = true;
            listen_down_ms_ = juce::Time::getMillisecondCounter();
            listen_was_on_ = bridge_.getBool(zlp::PSideOut::kID);
            if (!listen_was_on_) {
                bridge_.set(zlp::PSideOut::kID, 1.f, "Side-Chain Listen");
            }
        }
    }

    void SideChainPanel::mouseUp(const juce::MouseEvent& e) {
        if (e.eventComponent != &listen_) {
            return;
        }
        listen_pressed_ = false;
        const auto held = juce::Time::getMillisecondCounter() - listen_down_ms_ > kHoldMs;
        // a click toggles; a long press only listened while it was held
        const auto on = held ? false : !listen_was_on_;
        if (on != bridge_.getBool(zlp::PSideOut::kID)) {
            bridge_.set(zlp::PSideOut::kID, on ? 1.f : 0.f, "Side-Chain Listen");
        }
    }

    void SideChainPanel::visibilityChanged() {
        // a solo is not left playing behind a closed drawer
        if (!isVisible()) {
            eq_.setSolo(zlp::kBandNum);
        }
    }

    void SideChainPanel::resized() {
        auto b = getLocalBounds();
        b.removeFromTop(1);
        auto controls = b.removeFromLeft(300).reduced(14, 12);
        divider_x_ = 300.f;
        eq_.setBounds(b);
        eq_on_.setBounds(b.getX() + 10, b.getY() + 8, eq_on_.getIdealWidth() + 8, 22);
        eq_.setToolsInset(eq_on_.getRight() - b.getX() + 10);

        auto row = controls.removeFromTop(26);
        listen_.setBounds(row.removeFromRight(listen_.getIdealWidth() + 8));
        row.removeFromRight(6);
        const auto half = (row.getWidth() - 2) / 2;
        internal_.setBounds(row.removeFromLeft(half));
        row.removeFromLeft(2);
        external_.setBounds(row);
        controls.removeFromTop(10);
        row = controls.removeFromTop(26);
        swap_.setBounds(row.removeFromRight(swap_.getIdealWidth() + 8));
        row.removeFromRight(6);
        stereo_.setBounds(row);
        controls.removeFromTop(10);
        const auto w = controls.getWidth() / 4;
        for (auto* k : {&gain_, &link_, &amount1_, &amount2_}) {
            k->setBounds(controls.removeFromLeft(w).withHeight(size::kKnobHeight));
        }
    }

    void SideChainPanel::paint(juce::Graphics& g) {
        g.fillAll(colour::kWindow);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, static_cast<float>(getWidth()), 1.f));
        g.fillRect(juce::Rectangle<float>(divider_x_ - 1.f, 1.f, 1.f, static_cast<float>(getHeight())));
    }
}
