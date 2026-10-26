// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "control_deck.hpp"

#include "theme/dialogue_tokens.hpp"

namespace altvane::dialogue {
    using namespace tokens;

    // ---------------------------------------------------------------- StemCard

    StemCard::StemCard(const size_t stem, PluginProcessor& p, ui::ParamEditor& editor) :
        stem_(stem), p_ref_(p) {
        const juce::String name = params::kStemNames[stem];
        knob_.setStyle(ui::Knob::Style::kHero);
        ui::bindValue(knob_, editor, p.parameters_.getParameter(params::kLevelIDs[stem]), name);
        knob_.format = [](const float v) { return params::formatLevel(v); };
        knob_.setAccent(colour::stem(stem));
        knob_.setTitle(name + " level");

        solo_.setToggleable(true);
        solo_.setOnColour(colour::stem(stem));
        solo_.setTooltip("Solo " + name.toLowerCase() + ": hear it on its own");
        solo_.setTitle("Solo " + name.toLowerCase());
        solo_.on_click = [this] { if (on_solo) on_solo(); };

        curve_.setToggleable(true);
        curve_.setOnColour(colour::stem(stem));
        curve_.setTooltip(name + " sensitivity across the spectrum");
        curve_.setTitle(name + " sensitivity");
        curve_.on_click = [this] { if (on_curve) on_curve(); };

        for (auto* c : std::initializer_list<juce::Component*>{&knob_, &solo_, &curve_}) {
            addAndMakeVisible(c);
        }
    }

    void StemCard::sync(const dsp::Listen listen, const int focus) {
        knob_.setValue(p_ref_.parameters_.getRawParameterValue(params::kLevelIDs[stem_])->load());
        solo_.setOn(static_cast<int>(listen) - 1 == static_cast<int>(stem_));
        const auto focused = focus == static_cast<int>(stem_);
        curve_.setOn(focused);
        // a dot on the curve button when the curve is not flat, so a shaped stem is never a surprise
        bool changed = false;
        for (size_t n = 0; n < dsp::kSensitivityNodes; ++n) {
            changed = changed || std::abs(p_ref_.parameters_.getRawParameterValue(params::sensitivityID(stem_, n))->load()) > .005f;
        }
        if (focused != focused_ || changed != sensitivity_changed_) {
            focused_ = focused;
            sensitivity_changed_ = changed;
            repaint();
        }
    }

    void StemCard::resized() {
        auto b = getLocalBounds();
        auto buttons = b.removeFromBottom(30);
        b.removeFromBottom(4);
        knob_.setBounds(b);
        const auto w = 26 * 2 + 6;
        buttons = buttons.withSizeKeepingCentre(w, 26);
        solo_.setBounds(buttons.removeFromLeft(26));
        curve_.setBounds(buttons.removeFromRight(26));
    }

    void StemCard::paint(juce::Graphics& g) {
        if (sensitivity_changed_) {
            const auto r = curve_.getBounds().toFloat();
            g.setColour(colour::stem(stem_));
            g.fillEllipse(juce::Rectangle<float>(4.f, 4.f).withCentre({r.getRight() - 1.f, r.getY() + 1.f}));
        }
        if (focused_) {
            // ties the card to the curve above
            const auto x = static_cast<float>(getWidth()) * .5f;
            g.setColour(colour::stem(stem_).withAlpha(.9f));
            g.fillRoundedRectangle(juce::Rectangle<float>(28.f, 2.f).withCentre({x, 1.f}), 1.f);
        }
    }

    // ---------------------------------------------------------------- ControlDeck

    ControlDeck::ControlDeck(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p) {
        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            cards_[s] = std::make_unique<StemCard>(s, p, editor);
            auto& card = *cards_[s];
            card.on_solo = [this, s] {
                const auto solo = static_cast<dsp::Listen>(static_cast<int>(s) + 1);
                p_ref_.setListen(p_ref_.getListen() == solo ? dsp::Listen::kMix : solo);
            };
            card.on_curve = [this, s] {
                if (on_focus) {
                    on_focus(focus_ == static_cast<int>(s) ? -1 : static_cast<int>(s));
                }
            };
            addAndMakeVisible(card);
        }
        setTitle("Stem levels");
        setFocusContainerType(FocusContainerType::focusContainer);
    }

    void ControlDeck::tick(const int focus) {
        focus_ = focus;
        const auto listen = p_ref_.getListen();
        for (auto& card : cards_) {
            card->sync(listen, focus);
        }
    }

    void ControlDeck::resized() {
        auto b = getLocalBounds().reduced(0, static_cast<int>(size::kS4));
        const auto card_w = static_cast<int>(size::kCardWidth);
        const auto gap = std::clamp((b.getWidth() - card_w * 3) / 4, 8, 72);
        auto row = b.withSizeKeepingCentre(card_w * 3 + gap * 2, b.getHeight());
        for (auto& card : cards_) {
            card->setBounds(row.removeFromLeft(card_w));
            row.removeFromLeft(gap);
        }
    }

    void ControlDeck::paint(juce::Graphics& g) {
        g.fillAll(colour::kWindow);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, static_cast<float>(getWidth()), 1.f));
    }
}
