// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "match_panel.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/util/value_format.hpp"
#include "../theme/eq_tokens.hpp"

namespace altvane::eq {
    using namespace tokens;

    namespace {
        const std::array<const char*, 5> kTargetNames{"Side-chain", "Flat", "Natural", "Balance", "Reference File"};
        constexpr std::array<float, 4> kLimits{6.f, 12.f, 30.f, 60.f};
        constexpr float kHeaderH = 30.f;
    }

    MatchPanel::MatchPanel(MatchEngine& engine) : engine_(engine) {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &reference_, &limit_, &amount_, &tilt_, &bands_, &relearn_, &cancel_, &primary_, &close_
             }) {
            addAndMakeVisible(c);
        }
        reference_.setLabel("Reference");
        reference_.build_menu = [this] {
            juce::PopupMenu menu;
            menu.addSectionHeader("Match To");
            for (int i = 0; i < 4; ++i) {
                menu.addItem(i + 1, kTargetNames[static_cast<size_t>(i)], true,
                             static_cast<int>(engine_.getTarget()) == i);
            }
            menu.addItem(5, "Load Reference File...", true, engine_.getTarget() == MatchEngine::Target::kFile);
            menu.addSeparator();
            menu.addItem(6, "Save Reference File...", engine_.getFrame().valid);
            return menu;
        };
        reference_.on_result = [this](const int r) {
            if (r <= 4) {
                engine_.setTarget(static_cast<MatchEngine::Target>(r - 1));
                sync();
                return;
            }
            const auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile(JucePlugin_Manufacturer).getChildFile(JucePlugin_Name).getChildFile("Match");
            dir.createDirectory();
            if (r == 5) {
                chooser_ = std::make_unique<juce::FileChooser>("Load Reference", dir, "*.csv");
                chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [safe = juce::Component::SafePointer(this)](const juce::FileChooser& fc) {
                                          if (safe != nullptr && fc.getResult().existsAsFile()) {
                                              safe->engine_.loadTargetFile(fc.getResult());
                                              safe->sync();
                                          }
                                      });
            } else if (r == 6) {
                chooser_ = std::make_unique<juce::FileChooser>("Save Reference", dir.getChildFile("reference.csv"), "*.csv");
                chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::warnAboutOverwriting,
                                      [safe = juce::Component::SafePointer(this)](const juce::FileChooser& fc) {
                                          if (safe != nullptr && fc.getResult() != juce::File()) {
                                              safe->engine_.saveTargetFile(fc.getResult().withFileExtension("csv"));
                                          }
                                      });
            }
        };

        amount_.setLabel("Amount");
        amount_.setRange(juce::NormalisableRange<float>(0.f, 1.f, .01f), 1.f);
        amount_.format = [](const float v) { return format::percent(v * 100.f); };
        amount_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = format::parseNumber(t)) {
                return static_cast<float>(*v) / 100.f;
            }
            return std::nullopt;
        };
        amount_.on_change = [this](const float v) { engine_.setAmount(v); };

        tilt_.setLabel("Tilt");
        tilt_.setRange(juce::NormalisableRange<float>(-4.5f, 4.5f, .1f), 0.f);
        tilt_.format = [](const float v) { return format::gain(v, false) + " dB/oct"; };
        tilt_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = format::parseNumber(t.upToFirstOccurrenceOf("/", false, false))) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        tilt_.on_change = [this](const float v) { engine_.setTilt(v); };

        limit_.setLabel("Limit");
        limit_.build_menu = [this] {
            juce::PopupMenu menu;
            menu.addSectionHeader("Maximum Correction");
            for (size_t i = 0; i < kLimits.size(); ++i) {
                menu.addItem(static_cast<int>(i) + 1,
                             i + 1 == kLimits.size() ? juce::String("No Limit")
                                                     : juce::String(juce::CharPointer_UTF8("\xc2\xb1"))
                                                     + juce::String(kLimits[i], 0) + " dB",
                             true, std::abs(engine_.getLimit() - kLimits[i]) < .1f);
            }
            return menu;
        };
        limit_.on_result = [this](const int r) {
            engine_.setLimit(kLimits[static_cast<size_t>(r - 1)]);
            sync();
        };

        bands_.setLabel("Bands");
        bands_.format = [](const float v) { return juce::String(juce::roundToInt(v)); };
        bands_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = format::parseNumber(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        bands_.on_change = [this](const float v) {
            const auto n = static_cast<size_t>(juce::roundToInt(v));
            if (n != engine_.getNumApplied()) {
                engine_.applyBands(n);
            }
        };

        relearn_.setTooltip("Clear what has been learned and listen again");
        relearn_.on_click = [this] { engine_.relearn(); };
        cancel_.on_click = [this] { engine_.cancel(); };
        close_.setTooltip("Leave EQ Match without changes");
        close_.on_click = [this] { engine_.cancel(); };
        primary_.on_click = [this] {
            if (engine_.getState() == MatchEngine::State::kResult) {
                engine_.finish();
            } else {
                engine_.fit();
            }
        };
        sync();
    }

    MatchPanel::~MatchPanel() {
        stopTimer();
    }

    void MatchPanel::sync() {
        const auto state = engine_.getState();
        const auto target = static_cast<size_t>(engine_.getTarget());
        reference_.setText(kTargetNames[target]);
        const auto limit = engine_.getLimit();
        limit_.setText(limit > 50.f ? juce::String("No Limit")
                                    : juce::String(juce::CharPointer_UTF8("\xc2\xb1")) + juce::String(limit, 0) + " dB");
        amount_.setValue(engine_.getAmount());
        tilt_.setValue(engine_.getTilt());
        const auto result = state == MatchEngine::State::kResult;
        bands_.setVisible(result);
        if (result) {
            bands_.setRange(juce::NormalisableRange<float>(0.f, static_cast<float>(std::max<size_t>(engine_.getMaxBands(), 1)), 1.f),
                            static_cast<float>(engine_.getSuggested()));
            bands_.setValue(static_cast<float>(engine_.getNumApplied()));
        }
        primary_.setText(result ? "Done" : (state == MatchEngine::State::kFitting ? "Matching..." : "Match"));
        primary_.setEnabled(state != MatchEngine::State::kFitting);
        cancel_.setVisible(result);
        if (state == MatchEngine::State::kOff) {
            stopTimer();
        } else {
            startTimerHz(4);
        }
        updateStatus();
        resized();
        repaint();
    }

    void MatchPanel::timerCallback() {
        updateStatus();
    }

    void MatchPanel::updateStatus() {
        const auto& frame = engine_.getFrame();
        juce::String status, hint;
        switch (engine_.getState()) {
        case MatchEngine::State::kLearning:
            status = "Listening  " + juce::String(frame.seconds, frame.seconds < 10.f ? 1 : 0) + " s";
            hint = engine_.getTarget() == MatchEngine::Target::kSideChain
                       ? "Play the source and the side-chain reference, then Match. Draw on the curve to refine it."
                       : "Play the source for a few seconds, then Match. Draw on the curve to refine it.";
            break;
        case MatchEngine::State::kFitting:
            status = "Fitting  " + juce::String(engine_.getNumApplied()) + (engine_.getNumApplied() == 1 ? " band" : " bands");
            hint = "Bands are added one at a time, the most significant first.";
            break;
        case MatchEngine::State::kResult:
            status = juce::String(engine_.getNumApplied()) + " bands";
            hint = "Suggested " + juce::String(engine_.getSuggested()) + " of " + juce::String(engine_.getMaxBands())
                + ". Drag Bands to use more or fewer.";
            break;
        case MatchEngine::State::kOff:
            break;
        }
        if (status != status_ || hint != hint_) {
            status_ = status;
            hint_ = hint;
            repaint();
        }
    }

    void MatchPanel::resized() {
        auto b = getLocalBounds().toFloat().reduced(12.f, 0.f);
        auto header = b.removeFromTop(kHeaderH).reduced(0.f, 4.f);
        close_.setBounds(header.removeFromRight(22.f).toNearestInt());

        auto row = b.removeFromTop(44.f).reduced(0.f, 8.f);
        reference_.setBounds(row.removeFromLeft(150.f).toNearestInt());
        row.removeFromLeft(8.f);
        amount_.setBounds(row.removeFromLeft(118.f).toNearestInt());
        row.removeFromLeft(4.f);
        tilt_.setBounds(row.removeFromLeft(150.f).toNearestInt());
        row.removeFromLeft(8.f);
        limit_.setBounds(row.removeFromLeft(96.f).toNearestInt());
        relearn_.setBounds(row.removeFromRight(static_cast<float>(relearn_.getIdealWidth())).toNearestInt());

        auto bottom = b.removeFromTop(44.f).reduced(0.f, 6.f);
        primary_.setBounds(bottom.removeFromRight(std::max(110.f, static_cast<float>(primary_.getIdealWidth()))).toNearestInt());
        bottom.removeFromRight(8.f);
        if (cancel_.isVisible()) {
            cancel_.setBounds(bottom.removeFromRight(static_cast<float>(cancel_.getIdealWidth())).toNearestInt());
        }
        if (bands_.isVisible()) {
            bands_.setBounds(bottom.removeFromLeft(110.f).toNearestInt());
        }
    }

    void MatchPanel::paint(juce::Graphics& g) {
        const auto bounds = getLocalBounds().toFloat();
        for (int i = 3; i > 0; --i) {
            g.setColour(juce::Colours::black.withAlpha(.07f));
            g.fillRoundedRectangle(bounds.reduced(static_cast<float>(3 - i)).translated(0.f, static_cast<float>(i)),
                                   size::kRadiusLarge);
        }
        g.setColour(colour::kRaised.withAlpha(.97f));
        g.fillRoundedRectangle(bounds.reduced(2.f), size::kRadiusLarge);
        g.setColour(colour::kAccent.withAlpha(.35f));
        g.drawRoundedRectangle(bounds.reduced(2.5f), size::kRadiusLarge, 1.f);

        auto header = bounds.reduced(14.f, 0.f).removeFromTop(kHeaderH);
        g.setColour(colour::kTextPrimary);
        g.setFont(Fonts::get().font(type::kBody, Fonts::kSemiBold));
        g.drawText("EQ Match", header.removeFromLeft(80.f), juce::Justification::centredLeft);
        g.setColour(colour::kAccent);
        Fonts::get().drawTabular(g, status_, header.withTrimmedRight(30.f), juce::Justification::centredLeft,
                                 type::kSmall);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(12.f, kHeaderH, bounds.getWidth() - 24.f, 1.f));

        // hint sits in the free space of the bottom row
        auto hint_area = juce::Rectangle<float>(14.f, kHeaderH + 44.f, bounds.getWidth() - 28.f, 44.f).reduced(0.f, 6.f);
        if (bands_.isVisible()) {
            hint_area.removeFromLeft(122.f);
        }
        hint_area.removeFromRight(static_cast<float>(getWidth() - primary_.getX()) + (cancel_.isVisible() ? 90.f : 0.f));
        g.setColour(colour::kTextTertiary);
        g.setFont(Fonts::get().font(type::kSmall));
        g.drawFittedText(hint_, hint_area.toNearestInt(), juce::Justification::centredLeft, 2);
    }
}
