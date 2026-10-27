// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "control_deck.hpp"

#include <altvane_ui/theme/fonts.hpp>

#include "theme/deesser_tokens.hpp"

namespace altvane::deesser {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        void bindKnob(ui::Knob& knob, ui::ParamEditor& editor, juce::RangedAudioParameter* para, const juce::String& name) {
            knob.setStyle(ui::Knob::Style::kHero);
            ui::bindValue(knob, editor, para, name);
            knob.format = [](const float v) { return params::formatDb(v); };
            knob.parse = [para](const juce::String& t) -> std::optional<float> {
                // the parameter's own text conversion: "-24", "−24 dB"; without a number, nothing changes
                if (!t.containsAnyOf("0123456789")) {
                    return std::nullopt;
                }
                return para->convertFrom0to1(para->getValueForText(t));
            };
        }
    }

    ControlDeck::ControlDeck(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        bindKnob(threshold_, editor_, p.parameters_.getParameter(params::kThreshold), "Threshold");
        bindKnob(range_, editor_, p.parameters_.getParameter(params::kRange), "Range");
        range_.setAccent(colour::kReduction);

        learn_.setComponentID("learn");
        learn_.setTooltip("Learn the threshold: play a passage with esses (L)");
        learn_.setOnColour(colour::kSibilance);
        learn_.on_click = [this] { toggleLearn(); };

        vocal_.setTooltip("Vocal: tells esses from bright vowels; for a single voice");
        allround_.setTooltip("Allround: anything in the band above the threshold; for mixes, buses and instruments");
        wide_.setTooltip("Wide band: turn the whole signal down on each ess");
        split_.setTooltip("Split band: turn down only the high frequencies (transparent)");
        shelf_.setTooltip("Shelf: turn down everything above the band's low edge");
        bell_.setTooltip("Bell: turn down only the band, sparing the air above it");
        trigger_.setTooltip("Hear the detection band, to place it on the esses");
        delta_.setTooltip("Hear only what is being taken away");
        trigger_.setOnColour(colour::kSibilance);
        delta_.setOnColour(colour::kSibilance);

        vocal_.on_click = [this] { setChoice(params::kMode, 0, "Mode"); };
        allround_.on_click = [this] { setChoice(params::kMode, 1, "Mode"); };
        wide_.on_click = [this] { setChoice(params::kProcessing, 0, "Processing"); };
        split_.on_click = [this] { setChoice(params::kProcessing, 1, "Processing"); };
        // a shape only applies to split band: choosing one there switches to it
        auto shape = [this](const int index) {
            editor_.setNow({{p_ref_.parameters_.getParameter(params::kProcessing), 1.f},
                            {p_ref_.parameters_.getParameter(params::kShape), static_cast<float>(index)}},
                           "Change Shape");
        };
        shelf_.on_click = [shape] { shape(0); };
        bell_.on_click = [shape] { shape(1); };
        trigger_.on_click = [this] {
            p_ref_.setListen(p_ref_.getListen() == dsp::Listen::kTrigger ? dsp::Listen::kOff : dsp::Listen::kTrigger);
        };
        delta_.on_click = [this] {
            p_ref_.setListen(p_ref_.getListen() == dsp::Listen::kDelta ? dsp::Listen::kOff : dsp::Listen::kDelta);
        };
        vocal_.setComponentID(params::kMode);
        allround_.setComponentID(params::kMode);
        wide_.setComponentID(params::kProcessing);
        split_.setComponentID(params::kProcessing);
        shelf_.setComponentID(params::kShape);
        bell_.setComponentID(params::kShape);

        rows_[0] = {"MODE", {}, {&vocal_, &allround_}};
        rows_[1] = {"BAND", {}, {&wide_, &split_}};
        rows_[2] = {"SHAPE", {}, {&shelf_, &bell_}};
        rows_[3] = {"LISTEN", {}, {&trigger_, &delta_}};
        for (auto* c : std::initializer_list<juce::Component*>{&threshold_, &range_, &learn_}) {
            addAndMakeVisible(c);
        }
        for (auto& row : rows_) {
            for (auto* chip : row.chips) {
                addAndMakeVisible(chip);
            }
        }
        setTitle("De-esser controls");
        setFocusContainerType(FocusContainerType::focusContainer);
        updateLearnText();
    }

    void ControlDeck::setChoice(const char* id, const int index, const juce::String& name) {
        editor_.setNow({{p_ref_.parameters_.getParameter(id), static_cast<float>(index)}}, "Change " + name);
    }

    // ---------------------------------------------------------------- learn

    void ControlDeck::toggleLearn() {
        if (learner_.isLearning()) {
            finishLearn();
            return;
        }
        // only esses heard from now on
        float ignored;
        while (p_ref_.getEngine().pullEvent(ignored)) {
        }
        learner_.start();
        learn_message_ = {};
        pulse_ = 0.0;
        learn_fade_.setTarget(true);
        updateLearnText();
        repaint();
    }

    void ControlDeck::cancelLearn() {
        learn_due_ = false;
        if (learner_.isLearning()) {
            learner_.cancel();
            learn_fade_.setTarget(false);
            learn_message_ = "Cancelled";
            message_age_ = 0.0;
            updateLearnText();
            repaint();
        }
    }

    void ControlDeck::finishLearn() {
        learn_due_ = false;
        const auto heard = learner_.count();
        if (const auto threshold = learner_.finish(params::kThresholdMin, params::kThresholdMax)) {
            editor_.setNow({{p_ref_.parameters_.getParameter(params::kThreshold), std::round(*threshold * 10.f) / 10.f}},
                           "Learn Threshold");
            learn_message_ = "Set from " + juce::String(static_cast<int>(heard)) + " esses";
        } else {
            learn_message_ = "Too few esses";
        }
        message_age_ = 0.0;
        learn_fade_.setTarget(false);
        updateLearnText();
        repaint();
    }

    void ControlDeck::updateLearnText() {
        juce::String text;
        if (learner_.isLearning()) {
            text = "Listening " + juce::String(static_cast<int>(learner_.count())) + "/"
                   + juce::String(static_cast<int>(dsp::ThresholdLearner::kEnough));
        } else if (learn_message_.isNotEmpty()) {
            text = learn_message_;
        } else {
            text = "Learn";
        }
        learn_.setText(text);
        learn_.setOn(learner_.isLearning());
        learn_.setTitle(learner_.isLearning() ? "Stop learning and set the threshold" : "Learn threshold");
    }

    // ---------------------------------------------------------------- tick

    void ControlDeck::tick(const double dt) {
        const auto& tree = p_ref_.parameters_;
        threshold_.setValue(tree.getRawParameterValue(params::kThreshold)->load());
        range_.setValue(tree.getRawParameterValue(params::kRange)->load());
        const auto mode = juce::roundToInt(tree.getRawParameterValue(params::kMode)->load());
        const auto split = juce::roundToInt(tree.getRawParameterValue(params::kProcessing)->load()) == 1;
        const auto bell = juce::roundToInt(tree.getRawParameterValue(params::kShape)->load()) == 1;
        vocal_.setOn(mode == 0);
        allround_.setOn(mode == 1);
        wide_.setOn(!split);
        split_.setOn(split);
        shelf_.setOn(split && !bell);
        bell_.setOn(split && bell);
        // the shape stays reachable (picking one switches to split band) but steps back
        shelf_.setAlpha(split ? 1.f : .45f);
        bell_.setAlpha(split ? 1.f : .45f);
        const auto listen = p_ref_.getListen();
        trigger_.setOn(listen == dsp::Listen::kTrigger);
        delta_.setOn(listen == dsp::Listen::kDelta);

        float peak;
        while (p_ref_.getEngine().pullEvent(peak)) {
            if (learner_.isLearning() && !learn_due_) {
                learn_due_ = learner_.add(peak);
                updateLearnText();
                repaint();
            }
        }
        // setting the threshold would end an edit in progress (a knob being dragged): wait for it
        if (learn_due_ && !editor_.isEditing()) {
            finishLearn();
        }
        if (learner_.isLearning()) {
            pulse_ += dt;
            repaint(threshold_.getBounds().expanded(8));
        } else if (learn_message_.isNotEmpty() && (message_age_ += dt) > 2.5) {
            learn_message_ = {};
            updateLearnText();
        }
        if (learn_fade_.update(static_cast<float>(dt))) {
            repaint(threshold_.getBounds().expanded(8));
        }
    }

    // ---------------------------------------------------------------- layout

    void ControlDeck::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS4, size::kS4);
        auto knobs = b.removeFromLeft(240.f);
        auto left = knobs.removeFromLeft(124.f);
        auto learn = left.removeFromBottom(22.f);
        left.removeFromBottom(size::kS1);
        threshold_.setBounds(left.toNearestInt());
        learn_.setBounds(learn.withSizeKeepingCentre(std::min(left.getWidth(), 112.f), 22.f).toNearestInt());
        knobs.removeFromLeft(size::kS1);
        auto right = knobs.withTrimmedBottom(22.f + size::kS1);
        range_.setBounds(right.toNearestInt());

        b.removeFromLeft(size::kS3);
        divider_ = b.removeFromLeft(1.f).reduced(0.f, size::kS2);
        b.removeFromLeft(size::kS4);
        // four rows: a caption on the left, a segmented pair of chips on the right, centred together
        const auto row_h = 26.f;
        const auto gap = std::min(14.f, (b.getHeight() - row_h * 4.f) / 3.f);
        b = b.withSizeKeepingCentre(b.getWidth(), row_h * 4.f + gap * 3.f);
        for (auto& row : rows_) {
            auto r = b.removeFromTop(row_h);
            b.removeFromTop(gap);
            row.caption_area = r.removeFromLeft(50.f);
            const auto w = (r.getWidth() - 2.f * static_cast<float>(row.chips.size() - 1)) / static_cast<float>(row.chips.size());
            for (auto* chip : row.chips) {
                chip->setBounds(r.removeFromLeft(w).toNearestInt());
                r.removeFromLeft(2.f);
            }
        }
    }

    void ControlDeck::paint(juce::Graphics& g) {
        g.fillAll(colour::kWindow);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, 1.f, static_cast<float>(getHeight())));
        g.fillRect(divider_);
        g.setFont(captionFont());
        for (const auto& row : rows_) {
            g.setColour(colour::kTextTertiary);
            g.drawText(row.caption, row.caption_area, juce::Justification::centredLeft);
        }

        // while learning: a ring around the threshold that fills as esses are heard, breathing gently
        if (const auto fade = learn_fade_.getEased(); fade > 0.f) {
            const auto knob = threshold_.getBounds().toFloat();
            // the hero knob's dial sits under its caption and above its value
            const auto dial = knob.withTrimmedTop(22.f).withTrimmedBottom(30.f);
            const auto d = std::min({dial.getWidth(), dial.getHeight(), 132.f}) + 14.f;
            const auto ring = juce::Rectangle<float>(d, d).withCentre(dial.getCentre());
            const auto progress = static_cast<float>(learner_.count()) / static_cast<float>(dsp::ThresholdLearner::kEnough);
            const auto breathe = .55f + .25f * static_cast<float>(std::sin(pulse_ * 4.0));
            constexpr auto kStart = -juce::MathConstants<float>::pi * .75f;
            constexpr auto kSweep = juce::MathConstants<float>::pi * 1.5f;
            juce::Path track, done;
            track.addCentredArc(ring.getCentreX(), ring.getCentreY(), d * .5f, d * .5f, 0.f, kStart, kStart + kSweep, true);
            g.setColour(colour::kSibilance.withAlpha(.18f * fade));
            g.strokePath(track, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (progress > 0.f) {
                done.addCentredArc(ring.getCentreX(), ring.getCentreY(), d * .5f, d * .5f, 0.f, kStart,
                                   kStart + kSweep * progress, true);
                g.setColour(colour::kSibilance.withAlpha(breathe * fade));
                g.strokePath(done, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }
}
