// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "loudness_panel.hpp"

#include <altvane_ui/widgets/help_balloon.hpp>

namespace altvane::limiter {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kPad = 14.f, kHeader = 18.f, kRow = 26.f, kIntegrated = 50.f, kStatRow = 30.f;
        constexpr float kHistogram = 58.f, kButton = 28.f, kGap = 10.f;
        const juce::String kDot = juce::String::charToString(static_cast<juce::juce_wchar>(0x00B7));

        juce::String targetText(const LoudnessTarget& t) {
            const auto peak = level(t.true_peak, 1) + " dBTP";
            return t.lufs.has_value() ? juce::String(t.name) + "   " + level(*t.lufs, 1) + " LUFS " + kDot + " " + peak
                                      : juce::String(t.name) + "   " + peak;
        }

        juce::String duration(const double seconds) {
            const auto s = static_cast<int>(seconds);
            return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
        }
    }

    LoudnessPanel::LoudnessPanel(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        setTitle("Loudness");
        for (auto* c : std::initializer_list<juce::Component*>{&target_, &reset_, &auto_, &custom_lufs_, &custom_peak_, &match_}) {
            addAndMakeVisible(c);
        }
        target_.setLabel("Target");
        target_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = index(p_ref_.na_parameters_, zlstate::PLoudnessTarget::kID);
            for (size_t i = 0; i < kLoudnessTargets.size(); ++i) {
                const auto& t = kLoudnessTargets[i];
                const auto text = i == kCustomTarget ? juce::String("Custom") : targetText(t);
                menu.addItem(static_cast<int>(i) + 1, text, true, static_cast<int>(i) == current);
                if (i == 5 || i == 6) {
                    menu.addSeparator();
                }
            }
            return menu;
        };
        target_.on_result = [this](const int result) {
            if (result <= 0) {
                return;
            }
            auto* para = p_ref_.na_parameters_.getParameter(zlstate::PLoudnessTarget::kID);
            ui::ParamEditor::setNotifyingHost(para, para->convertTo0to1(static_cast<float>(result - 1)));
        };
        ui::setHelp(target_, "Delivery target",
                    "Where the master is going. Sets the integrated loudness to aim for and the true-peak limit "
                    "the readouts are checked against.");

        reset_.on_click = [this] { p_ref_.resetLoudness(); };
        ui::setHelp(reset_, "Reset", "Start a new loudness measurement.");
        auto_.setComponentID(zlstate::PLoudnessAutoReset::kID);
        auto_.on_click = [this] {
            auto* para = p_ref_.na_parameters_.getParameter(zlstate::PLoudnessAutoReset::kID);
            ui::ParamEditor::setNotifyingHost(para, para->getValue() > .5f ? 0.f : 1.f);
        };
        ui::setHelp(auto_, "Auto reset", "Start a new measurement whenever playback starts. Off, the measurement runs "
                                         "on across stops until you reset it.");

        bindCustom(custom_lufs_, zlstate::PCustomTargetLUFS::kID, "LUFS", "");
        bindCustom(custom_peak_, zlstate::PCustomTargetTruePeak::kID, "dBTP", "");
        ui::setHelp(custom_lufs_, "Target loudness", "The integrated loudness to aim for.");
        ui::setHelp(custom_peak_, "True-peak limit", "The highest true peak the delivery allows.");

        match_.on_click = [this] { matchTarget(); };
        ui::setHelp(match_, "Match target",
                    "Moves the gain by the difference between the integrated loudness and the target, then starts "
                    "a new measurement. Limiting makes it approximate: play the section again and repeat to settle.");
    }

    void LoudnessPanel::bindCustom(ui::ValueField& field, const char* id, const juce::String& name,
                                   const juce::String& unit) {
        auto* para = p_ref_.na_parameters_.getParameter(id);
        field.setLabel(name);
        field.setFramed(true);
        field.setRange(para->getNormalisableRange(), para->convertFrom0to1(para->getDefaultValue()));
        field.format = [unit](const float v) { return level(v, 1) + unit; };
        field.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = ui::format::parseNumber(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        // part of the session, not of the edit history
        field.on_change = [para](const float v) { ui::ParamEditor::setNotifyingHost(para, para->convertTo0to1(v)); };
    }

    float LoudnessPanel::getPreferredHeight(const bool custom, const bool histogram) {
        return kPad * 2.f + kHeader + 8.f + kRow + (custom ? kRow + 6.f : 0.f) + kGap + kIntegrated + kGap +
               4.f * kStatRow + (histogram ? kGap + kHistogram : 0.f) + kGap + kButton;
    }

    void LoudnessPanel::resized() {
        auto b = getLocalBounds().toFloat().reduced(kPad);
        header_ = b.removeFromTop(kHeader);
        reset_.setBounds(header_.removeFromRight(static_cast<float>(reset_.getIdealWidth())).toNearestInt()
                         .withSizeKeepingCentre(reset_.getIdealWidth(), 20));
        header_.removeFromRight(4.f);
        auto_.setBounds(header_.removeFromRight(static_cast<float>(auto_.getIdealWidth())).toNearestInt()
                        .withSizeKeepingCentre(auto_.getIdealWidth(), 20));
        b.removeFromTop(8.f);
        target_.setBounds(b.removeFromTop(kRow).toNearestInt());
        if (custom_) {
            b.removeFromTop(6.f);
            auto row = b.removeFromTop(kRow);
            custom_lufs_.setBounds(row.removeFromLeft(row.getWidth() * .5f - 3.f).toNearestInt());
            row.removeFromLeft(6.f);
            custom_peak_.setBounds(row.toNearestInt());
        }
        custom_lufs_.setVisible(custom_);
        custom_peak_.setVisible(custom_);
        b.removeFromTop(kGap);
        integrated_ = b.removeFromTop(kIntegrated);
        b.removeFromTop(kGap);
        match_.setBounds(b.removeFromBottom(kButton).toNearestInt());
        b.removeFromBottom(kGap);
        stats_ = b.removeFromTop(4.f * kStatRow);
        show_histogram_ = b.getHeight() >= kHistogram + kGap;
        histogram_ = show_histogram_ ? b.withTrimmedTop(b.getHeight() - kHistogram) : juce::Rectangle<float>{};
    }

    std::optional<float> LoudnessPanel::getGainToTarget() const {
        if (!r_.has_integrated || !current_.lufs.has_value() || r_.seconds < 1.0) {
            return std::nullopt;
        }
        const auto gain = value(p_ref_.parameters_, zlp::PInputGain::kID);
        const auto wanted = std::clamp(gain + (*current_.lufs - r_.integrated), zlp::PInputGain::kRange.start,
                                       zlp::PInputGain::kRange.end);
        return wanted - gain;
    }

    void LoudnessPanel::matchTarget() {
        const auto delta = getGainToTarget();
        if (!delta.has_value() || std::abs(*delta) < .05f) {
            return;
        }
        auto* gain = p_ref_.parameters_.getParameter(zlp::PInputGain::kID);
        editor_.setNow({{gain, value(p_ref_.parameters_, zlp::PInputGain::kID) + *delta}}, "Match Loudness Target");
        p_ref_.resetLoudness();
    }

    void LoudnessPanel::tick(const LoudnessReadings& loudness, const float correlation) {
        r_ = loudness;
        correlation_ = correlation;
        auto_.setOn(value(p_ref_.na_parameters_, zlstate::PLoudnessAutoReset::kID) > .5f);
        current_ = currentTarget(p_ref_.na_parameters_);
        const auto custom = index(p_ref_.na_parameters_, zlstate::PLoudnessTarget::kID) ==
                            static_cast<int>(kCustomTarget);
        if (custom != custom_) {
            custom_ = custom;
            if (auto* parent = getParentComponent()) {
                parent->resized();
            }
            resized();
        }
        target_.setText(custom_ ? juce::String("Custom") : targetText(current_));
        if (custom_) {
            custom_lufs_.setValue(value(p_ref_.na_parameters_, zlstate::PCustomTargetLUFS::kID));
            custom_peak_.setValue(value(p_ref_.na_parameters_, zlstate::PCustomTargetTruePeak::kID));
        }

        const auto delta = getGainToTarget();
        juce::String text;
        if (!current_.lufs.has_value()) {
            text = "No loudness target";
        } else if (!delta.has_value()) {
            text = "Match target";
        } else if (std::abs(*delta) < .05f) {
            text = "On target";
        } else {
            text = "Match target   " + ui::format::gain(*delta, true, true);
        }
        match_.setEnabled(delta.has_value() && std::abs(*delta) >= .05f);
        if (text != last_text_) {
            last_text_ = text;
            match_.setText(text);
        }
        repaint();
    }

    void LoudnessPanel::paint(juce::Graphics& g) {
        paintCard(g, getLocalBounds().toFloat().reduced(1.f), size::kRadiusLarge);

        auto header = header_;
        g.setFont(captionFont());
        g.setColour(colour::kTextSecondary);
        g.drawText("LOUDNESS", header, juce::Justification::centredLeft);
        g.setColour(colour::kTextTertiary);
        Fonts::get().drawTabular(g, duration(r_.seconds), header.withTrimmedRight(8.f), juce::Justification::centredRight,
                                 type::kSmall);

        // integrated, large, with how far it is from the target
        auto b = integrated_;
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("INTEGRATED", b.removeFromTop(12.f), juce::Justification::centredLeft);
        const auto value_text = r_.has_integrated ? level(r_.integrated, 1) : ui::format::kMinus + ui::format::kMinus;
        const auto big = 30.f;
        const auto w = Fonts::get().getTabularWidth(value_text, big, Fonts::kSemiBold);
        g.setColour(r_.has_integrated ? colour::kTextPrimary : colour::kTextTertiary);
        Fonts::get().drawTabular(g, value_text, b.withWidth(w + 2.f), juce::Justification::centredLeft, big,
                                 Fonts::kSemiBold);
        g.setFont(Fonts::get().font(type::kSmall, Fonts::kSemiBold));
        g.setColour(colour::kTextSecondary);
        g.drawText("LUFS", b.withTrimmedLeft(w + 6.f).withTrimmedTop(10.f), juce::Justification::centredLeft);
        if (r_.has_integrated && current_.lufs.has_value()) {
            const auto off = r_.integrated - *current_.lufs;
            const auto c = targetColour(off);
            const auto pill_text = level(off, 1, true) + " LU";
            const auto pw = Fonts::get().getTabularWidth(pill_text, type::kBody, Fonts::kSemiBold) + 16.f;
            const auto pill = juce::Rectangle<float>(b.getRight() - pw, b.getCentreY() - 11.f, pw, 22.f);
            g.setColour(c.withAlpha(.16f));
            g.fillRoundedRectangle(pill, 11.f);
            g.setColour(c);
            Fonts::get().drawTabular(g, pill_text, pill, juce::Justification::centred, type::kBody, Fonts::kSemiBold);
        }

        paintStats(g);
        if (show_histogram_) {
            paintHistogram(g);
        }
    }

    void LoudnessPanel::paintStats(juce::Graphics& g) const {
        const auto peak_over = r_.true_peak_max > current_.true_peak + .05f;
        const auto plr = r_.has_integrated && r_.true_peak_max > LoudnessReadings::kNoLevel
                             ? level(r_.true_peak_max - r_.integrated, 1) + " LU"
                             : juce::String("--");
        const auto corr = p_ref_.getNumMainChannels() == 1
                              ? juce::String("mono")
                              : std::isfinite(correlation_) ? level(correlation_, 2, true) : juce::String("--");
        const auto lufs = [](const float v) {
            return v > LoudnessReadings::kNoLevel ? level(v, 1) : juce::String("--");
        };
        struct Stat {
            const char* caption;
            juce::String value;
            juce::Colour colour;
        };
        const std::array<Stat, 8> stats{
            {
                {"SHORT-TERM", lufs(r_.short_term), colour::kTextPrimary},
                {"MOMENTARY", lufs(r_.momentary), colour::kTextPrimary},
                {"MAX SHORT-TERM", lufs(r_.max_short_term), colour::kTextSecondary},
                {"MAX MOMENTARY", lufs(r_.max_momentary), colour::kTextSecondary},
                {"RANGE", r_.has_range ? level(r_.range, 1) + " LU" : juce::String("--"), colour::kTextPrimary},
                {"TRUE PEAK", r_.true_peak_max > LoudnessReadings::kNoLevel ? level(r_.true_peak_max, 1) : "--",
                 peak_over ? colour::kDanger : colour::kTextPrimary},
                {"PLR", plr, colour::kTextSecondary},
                {"CORRELATION", corr, std::isfinite(correlation_) && correlation_ < 0.f ? colour::kWarning
                                                                                        : colour::kTextSecondary},
            }
        };
        const auto col_w = stats_.getWidth() * .5f;
        for (size_t i = 0; i < stats.size(); ++i) {
            const auto col = static_cast<float>(i % 2), row = static_cast<float>(i / 2);
            auto cell = juce::Rectangle<float>(stats_.getX() + col * col_w, stats_.getY() + row * kStatRow,
                                               col_w - (col == 0.f ? 8.f : 0.f), kStatRow);
            if (row > 0.f) {
                g.setColour(colour::kHairline);
                g.fillRect(cell.withHeight(1.f));
            }
            cell.removeFromTop(4.f);
            g.setFont(captionFont());
            g.setColour(colour::kTextTertiary);
            g.drawText(stats[i].caption, cell.removeFromTop(11.f), juce::Justification::centredLeft);
            g.setColour(stats[i].colour);
            Fonts::get().drawTabular(g, stats[i].value, cell, juce::Justification::centredLeft, type::kValue);
        }
    }

    void LoudnessPanel::paintHistogram(juce::Graphics& g) const {
        auto b = histogram_;
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("SHORT-TERM DISTRIBUTION", b.removeFromTop(12.f), juce::Justification::centredLeft);
        b.removeFromTop(4.f);
        // a window of the scale around the music (or the target, before there is any)
        const auto& h = r_.histogram;
        int first = -1, last = -1;
        float peak = 0.f;
        for (int i = 0; i < static_cast<int>(h.size()); ++i) {
            if (h[static_cast<size_t>(i)] > 0.f) {
                first = first < 0 ? i : first;
                last = i;
                peak = std::max(peak, h[static_cast<size_t>(i)]);
            }
        }
        const auto centre = current_.lufs.value_or(-14.f) - static_cast<float>(LoudnessReadings::kHistogramLow);
        auto lo = static_cast<int>(centre) - 12, hi = static_cast<int>(centre) + 8;
        if (first >= 0) {
            lo = std::min(lo, first - 1);
            hi = std::max(hi, last + 1);
        }
        lo = std::max(lo, 0);
        hi = std::min(hi, static_cast<int>(h.size()) - 1);
        const auto bins = static_cast<float>(hi - lo + 1);
        const auto bw = b.getWidth() / bins;
        g.setColour(colour::kControl);
        g.fillRoundedRectangle(b, 3.f);
        for (int i = lo; i <= hi; ++i) {
            const auto v = peak > 0.f ? h[static_cast<size_t>(i)] / peak : 0.f;
            if (v <= 0.f) {
                continue;
            }
            const auto x = b.getX() + static_cast<float>(i - lo) * bw;
            const auto bar = juce::Rectangle<float>(x + .5f, b.getBottom() - v * (b.getHeight() - 2.f), bw - 1.f,
                                                    v * (b.getHeight() - 2.f));
            g.setColour(colour::kLoudness.withAlpha(.35f + .45f * v));
            g.fillRect(bar);
        }
        const auto x_of = [&](const float lufs) {
            return b.getX() + (lufs - static_cast<float>(LoudnessReadings::kHistogramLow + lo)) * bw;
        };
        if (current_.lufs.has_value()) {
            const auto x = x_of(*current_.lufs);
            const float dashes[] = {2.f, 2.f};
            g.setColour(colour::kTextPrimary.withAlpha(.6f));
            g.drawDashedLine({x, b.getY(), x, b.getBottom()}, dashes, 2, 1.f);
        }
        g.setFont(Fonts::get().font(9.f));
        g.setColour(colour::kTextTertiary);
        g.drawText(level(static_cast<float>(LoudnessReadings::kHistogramLow + lo), 0),
                   b.withHeight(12.f).translated(3.f, b.getHeight() - 12.f), juce::Justification::centredLeft);
        g.drawText(level(static_cast<float>(LoudnessReadings::kHistogramLow + hi + 1), 0),
                   b.withHeight(12.f).translated(-3.f, b.getHeight() - 12.f), juce::Justification::centredRight);
    }
}
