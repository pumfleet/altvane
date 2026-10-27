// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "control_deck.hpp"

#include "theme/leveler_tokens.hpp"

namespace altvane::leveler {
    using namespace tokens;

    ControlDeck::ControlDeck(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        setTitle("Controls");
        setFocusContainerType(FocusContainerType::focusContainer);
        struct Binding {
            ui::Knob& knob;
            const char* id;
            const char* name;
            const char* tip;
            bool percent;
        };
        const std::array<Binding, 5> bindings{
            {
                {target_, params::kTarget, "Target", "Where the voice should sit, in LUFS-style loudness", false},
                {speed_, params::kSpeed, "Speed", "How fast the rider moves; cuts are twice as fast as boosts", true},
                {sensitivity_, params::kSensitivity, "Sensitivity",
                 "How quiet a voice still counts: raises or lowers the floor below the target", false},
                {guard_, params::kGuard, "Guard", "How firmly breaths, sibilants and noise are kept from moving the rider", true},
                {music_, params::kMusic, "Music",
                 "How far the target follows the music on the side chain (Music input) as it gets louder or quieter", true},
            }
        };
        for (const auto& b : bindings) {
            ui::bindValue(b.knob, editor, p.parameters_.getParameter(b.id), b.name);
            if (b.percent) {
                b.knob.format = [](const float v) { return params::formatPercent(v); };
                b.knob.parse = [](const juce::String& t) -> std::optional<float> {
                    const auto clean = t.retainCharacters("+-.0123456789");
                    return clean.containsAnyOf("0123456789") ? std::optional<float>(clean.getFloatValue() / 100.f) : std::nullopt;
                };
            } else {
                b.knob.format = [](const float v) { return params::formatDb(v); };
            }
            b.knob.setDescription(b.tip);
            b.knob.setAccent(colour::kAccent);
            addAndMakeVisible(b.knob);
        }
        target_.setStyle(ui::Knob::Style::kHero);
        sensitivity_.setBipolar(true);

        learn_.setTooltip("Listen to the voice and set the target to its loudness");
        learn_.on_click = [this] { toggleLearn(); };
        addAndMakeVisible(learn_);
        lookahead_.setTooltip("Lookahead: 30 ms latency, so the rider moves before a phrase starts. "
                              "Off for zero latency (tracking, live)");
        lookahead_.setComponentID(params::kLookahead);
        lookahead_.on_click = [this] { editor_.toggle(p_ref_.parameters_.getParameter(params::kLookahead), "Lookahead"); };
        addAndMakeVisible(lookahead_);
        tick();
    }

    ui::Knob& ControlDeck::getKnob(const char* id) {
        const juce::String s(id);
        if (s == params::kSpeed) return speed_;
        if (s == params::kSensitivity) return sensitivity_;
        if (s == params::kGuard) return guard_;
        if (s == params::kMusic) return music_;
        return target_;
    }

    void ControlDeck::toggleLearn() {
        auto& engine = p_ref_.getEngine();
        if (!engine.isLearning()) {
            engine.startLearn();
            learn_message_ = "Listening...";
        } else if (const auto learned = engine.stopLearn()) {
            auto* target = p_ref_.parameters_.getParameter(params::kTarget);
            const auto& range = target->getNormalisableRange();
            editor_.setNow({{target, std::clamp(*learned, range.start, range.end)}}, "Learn Target");
            learn_message_ = "Learned " + params::formatDb(*learned);
        } else {
            learn_message_ = "Not enough voice heard";
        }
        tick();
        repaint();
    }

    void ControlDeck::tick() {
        const auto& t = p_ref_.parameters_;
        for (auto* knob : {&target_, &speed_, &sensitivity_, &guard_, &music_}) {
            knob->setValue(t.getRawParameterValue(knob->getComponentID())->load());
        }
        // the music amount only matters with something on the side chain
        music_.setEnabledLook(p_ref_.getKeyChannels() > 0);
        const auto learning = p_ref_.getEngine().isLearning();
        learn_.setText(learning ? "Stop" : "Learn");
        learn_.setPrimary(learning);
        lookahead_.setOn(t.getRawParameterValue(params::kLookahead)->load() > .5f);
    }

    void ControlDeck::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS4, size::kS2);
        target_.setBounds(b.removeFromLeft(150.f).toNearestInt());
        divider_ = b.removeFromLeft(size::kS5).withTrimmedTop(size::kS4).withTrimmedBottom(size::kS4);

        auto side = b.removeFromRight(132.f);
        side = side.withSizeKeepingCentre(side.getWidth(), 24.f * 2.f + size::kS2 + 16.f);
        learn_.setBounds(side.removeFromTop(24.f).toNearestInt());
        side.removeFromTop(size::kS2);
        lookahead_.setBounds(side.removeFromTop(24.f).toNearestInt());
        message_area_ = side;

        b.removeFromRight(size::kS4);
        const auto knob_w = std::min(b.getWidth() / 4.f, 96.f);
        auto row = b.withSizeKeepingCentre(knob_w * 4.f, std::min(b.getHeight(), 112.f));
        for (auto* knob : {&speed_, &sensitivity_, &guard_, &music_}) {
            knob->setBounds(row.removeFromLeft(knob_w).toNearestInt());
        }
    }

    void ControlDeck::paint(juce::Graphics& g) {
        g.fillAll(colour::kWindow);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, static_cast<float>(getWidth()), 1.f));
        g.fillRect(juce::Rectangle<float>(divider_.getCentreX(), divider_.getY(), 1.f, divider_.getHeight()));
        if (learn_message_.isNotEmpty()) {
            g.setColour(colour::kTextTertiary);
            g.setFont(ui::Fonts::get().font(type::kSmall));
            g.drawText(learn_message_, message_area_, juce::Justification::centred);
        }
    }
}
