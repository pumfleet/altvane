// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "control_card.hpp"

#include <altvane_ui/widgets/help_balloon.hpp>

namespace altvane::limiter {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kPadX = 14.f, kPadY = 10.f, kDivider = 21.f;
        constexpr float kCharacterWidth = 150.f, kHeroWidth = 88.f, kChipColumn = 82.f;
        constexpr float kKnobWidth = 60.f, kSliderWidth = 124.f;

        std::array<float, 5> timingValues(juce::AudioProcessorValueTreeState& tree) {
            std::array<float, 5> v{};
            for (size_t i = 0; i < kCharacterParameterIDs.size(); ++i) {
                v[i] = value(tree, kCharacterParameterIDs[i]);
            }
            return v;
        }
    }

    ControlCard::ControlCard(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        setTitle("Controls");
        auto& tree = p.parameters_;
        for (auto* c : std::initializer_list<juce::Component*>{
                 &character_, &gain_, &ceiling_, &attack_, &release_, &recovery_, &lookahead_, &link_,
                 &true_peak_, &unity_, &collapse_}) {
            addAndMakeVisible(c);
        }

        character_.setLabel("Character");
        character_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = findCharacter(timingValues(p_ref_.parameters_));
            for (size_t i = 0; i < kCharacters.size(); ++i) {
                menu.addItem(static_cast<int>(i) + 1, kCharacters[i].name, true, current == i);
            }
            return menu;
        };
        character_.on_result = [this](const int result) {
            if (result > 0) {
                applyCharacter(static_cast<size_t>(result - 1));
            }
        };
        ui::setHelp(character_, "Character",
                    "A starting point for the timing: sets lookahead, attack, release, recovery and link together. "
                    "Change any of them and it reads Custom.");

        gain_.setStyle(ui::Knob::Style::kHero);
        ui::bindValue(gain_, editor_, tree.getParameter(zlp::PInputGain::kID), "Gain");
        gain_.format = [](const float v) { return ui::format::gain(v, true, true); };
        ui::setHelp(gain_, "Gain", "How hard the signal is pushed into the limiter. You can also drag the waveform "
                                   "up or down in the display.");

        ceiling_.setStyle(ui::Knob::Style::kHero);
        ui::bindValue(ceiling_, editor_, tree.getParameter(zlp::POutputCeiling::kID), "Ceiling");
        ceiling_.format = [this](const float v) {
            return level(v, 1) + (value(p_ref_.parameters_, zlp::PTruePeak::kID) > .5f ? " dBTP" : " dB");
        };
        ui::setHelp(ceiling_, "Ceiling", "The highest level the output may reach. Drag its line in the display, too.");

        const auto ms = [](const float v) { return ui::format::ms(v); };
        ui::bindValue(attack_, editor_, tree.getParameter(zlp::PAttack::kID), "Attack");
        attack_.format = ms;
        ui::setHelp(attack_, "Attack", "How quickly the slow stage takes over sustained reduction. Longer leaves more "
                                       "of the work to the fast peak stage.");
        ui::bindValue(release_, editor_, tree.getParameter(zlp::PRelease::kID), "Release");
        release_.format = ms;
        ui::setHelp(release_, "Release", "How slowly the sustained reduction lets go. Longer is smoother, shorter is "
                                         "louder and can pump.");
        ui::bindValue(recovery_, editor_, tree.getParameter(zlp::PRecovery::kID), "Recovery");
        recovery_.format = ms;
        ui::setHelp(recovery_, "Recovery", "How fast the peak stage recovers after a peak. It adapts: sustained limiting "
                                           "stretches it up to five times.");

        ui::bindValue(lookahead_, editor_, tree.getParameter(zlp::PLookahead::kID), "Lookahead");
        lookahead_.format = [](const float v) { return juce::String(v, 2) + " ms"; };
        ui::setHelp(lookahead_, "Lookahead", "How far ahead the limiter sees peaks coming. Longer is cleaner on "
                                             "transients, shorter keeps their bite. The latency stays the same.");
        ui::bindValue(link_, editor_, tree.getParameter(zlp::PChannelDelta::kID), "Unlink");
        link_.format = [](const float v) { return v < .005f ? juce::String("Linked") : juce::String(v, 1) + " dB"; };
        ui::setHelp(link_, "Unlink", "How far the two channels' gain reduction may differ. Linked (0 dB) keeps the "
                                     "stereo image still; more lets each side limit on its own.");

        true_peak_.setComponentID(zlp::PTruePeak::kID);
        true_peak_.on_click = [this] {
            editor_.toggle(p_ref_.parameters_.getParameter(zlp::PTruePeak::kID), "True Peak");
        };
        ui::setHelp(true_peak_, "True peak", "Limit the reconstructed waveform, not just the samples, so the "
                                             "ceiling holds after conversion and encoding (ITU-R BS.1770).");
        unity_.setComponentID(zlp::PInputOutputLink::kID);
        unity_.on_click = [this] {
            const auto on = index(p_ref_.parameters_, zlp::PInputOutputLink::kID) > 0;
            editor_.setNow({{p_ref_.parameters_.getParameter(zlp::PInputOutputLink::kID), on ? 0.f : 1.f}},
                           "Unity Gain");
        };
        ui::setHelp(unity_, "Unity gain", "Take the gain back off the output, so you hear what the limiting does at "
                                          "the same level.");

        collapse_.setTooltip("Hide the controls");
        collapse_.on_click = [this] {
            if (on_collapse) {
                on_collapse();
            }
        };
        tick();
    }

    float ControlCard::getPreferredWidth() {
        return kPadX * 2.f + kCharacterWidth + kDivider + kHeroWidth * 2.f + 6.f + kChipColumn + kDivider +
               kKnobWidth * 3.f + 8.f + kSliderWidth;
    }

    void ControlCard::applyCharacter(const size_t i) {
        const auto& c = kCharacters[std::min(i, kCharacters.size() - 1)];
        const auto values = characterValues(c);
        std::vector<std::pair<juce::RangedAudioParameter*, float>> changes;
        for (size_t k = 0; k < values.size(); ++k) {
            changes.emplace_back(p_ref_.parameters_.getParameter(kCharacterParameterIDs[k]), values[k]);
        }
        editor_.setNow(changes, juce::String("Character: ") + c.name);
    }

    void ControlCard::tick() {
        auto& tree = p_ref_.parameters_;
        gain_.setValue(value(tree, zlp::PInputGain::kID));
        ceiling_.setValue(value(tree, zlp::POutputCeiling::kID));
        attack_.setValue(value(tree, zlp::PAttack::kID));
        release_.setValue(value(tree, zlp::PRelease::kID));
        recovery_.setValue(value(tree, zlp::PRecovery::kID));
        lookahead_.setValue(value(tree, zlp::PLookahead::kID));
        link_.setValue(value(tree, zlp::PChannelDelta::kID));
        true_peak_.setOn(value(tree, zlp::PTruePeak::kID) > .5f);
        unity_.setOn(index(tree, zlp::PInputOutputLink::kID) > 0);

        const auto found = findCharacter(timingValues(tree));
        const auto name = found.has_value() ? juce::String(kCharacters[*found].name) : juce::String("Custom");
        if (name != character_name_) {
            character_name_ = name;
            character_text_ = found.has_value() ? juce::String(kCharacters[*found].description)
                                                : juce::String("Your own timing. Pick a character to start again from "
                                                               "one of the presets.");
            character_.setText(name);
            repaint();
        }
    }

    void ControlCard::resized() {
        auto b = getLocalBounds().toFloat().reduced(kPadX, kPadY);
        collapse_.setBounds(juce::Rectangle<int>(22, 22).withPosition(getWidth() - 28, 6));

        auto col = b.removeFromLeft(kCharacterWidth);
        character_caption_ = col.removeFromTop(16.f);
        character_.setBounds(col.removeFromTop(26.f).toNearestInt());
        col.removeFromTop(8.f);
        description_ = col;

        dividers_[0] = b.getX() + kDivider * .5f;
        b.removeFromLeft(kDivider);
        gain_.setBounds(b.removeFromLeft(kHeroWidth).toNearestInt());
        ceiling_.setBounds(b.removeFromLeft(kHeroWidth).toNearestInt());
        b.removeFromLeft(6.f);
        {
            auto chips = b.removeFromLeft(kChipColumn).withSizeKeepingCentre(kChipColumn, 54.f);
            true_peak_.setBounds(chips.removeFromTop(24.f).toNearestInt());
            chips.removeFromTop(6.f);
            unity_.setBounds(chips.removeFromTop(24.f).toNearestInt());
        }
        dividers_[1] = b.getX() + kDivider * .5f;
        b.removeFromLeft(kDivider);
        const auto knob_area = b.removeFromLeft(kKnobWidth * 3.f).withTrimmedTop(8.f);
        auto k = knob_area;
        attack_.setBounds(k.removeFromLeft(kKnobWidth).toNearestInt());
        release_.setBounds(k.removeFromLeft(kKnobWidth).toNearestInt());
        recovery_.setBounds(k.removeFromLeft(kKnobWidth).toNearestInt());
        b.removeFromLeft(8.f);
        auto sliders = b.removeFromLeft(kSliderWidth).withSizeKeepingCentre(kSliderWidth, 34.f * 2.f + 12.f)
                       .translated(0.f, 4.f);
        lookahead_.setBounds(sliders.removeFromTop(34.f).toNearestInt());
        sliders.removeFromTop(12.f);
        link_.setBounds(sliders.removeFromTop(34.f).toNearestInt());
    }

    void ControlCard::paint(juce::Graphics& g) {
        paintCard(g, getLocalBounds().toFloat().reduced(1.f), size::kCardRadius);
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("CHARACTER", character_caption_, juce::Justification::centredLeft);

        juce::AttributedString text;
        text.setWordWrap(juce::AttributedString::byWord);
        text.setLineSpacing(2.f);
        text.append(character_text_, Fonts::get().font(type::kSmall), colour::kTextSecondary);
        juce::TextLayout layout;
        layout.createLayout(text, description_.getWidth());
        layout.draw(g, description_);

        g.setColour(colour::kHairline);
        for (const auto x : dividers_) {
            g.fillRect(juce::Rectangle<float>(x, kPadY + 4.f, 1.f, static_cast<float>(getHeight()) - 2.f * kPadY - 8.f));
        }
    }
}
