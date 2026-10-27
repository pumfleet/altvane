// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "history_view.hpp"

namespace altvane::limiter {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kPadTop = 30.f, kPadBottom = 10.f, kAxisWidth = 40.f;
        constexpr float kCeilingGrab = 6.f;
        constexpr float kLevelFallPerSecond = 36.f, kReductionRisePerSecond = 60.f;

        void tag(juce::Graphics& g, const juce::String& text, const float right, const float centre_y,
                 const juce::Colour colour, const bool filled) {
            const auto font = Fonts::get().font(type::kCaption, Fonts::kSemiBold).withExtraKerningFactor(.04f);
            const auto w = juce::GlyphArrangement::getStringWidth(font, text) + 14.f;
            const juce::Rectangle<float> box(right - w, centre_y - 9.f, w, 18.f);
            g.setColour(filled ? colour : colour::kRaised.withAlpha(.94f));
            g.fillRoundedRectangle(box, size::kRadiusSmall);
            g.setColour(colour.withMultipliedAlpha(.6f));
            g.drawRoundedRectangle(box.reduced(.5f), size::kRadiusSmall, 1.f);
            g.setColour(filled ? colour::kWindow : colour);
            g.setFont(font);
            g.drawText(text, box, juce::Justification::centred);
        }
    }

    HistoryView::HistoryView(PluginProcessor& p, ui::ParamEditor& editor, const LevelFeed& feed) :
        p_ref_(p), editor_(editor), feed_(feed),
        gain_(p.parameters_.getParameter(zlp::PInputGain::kID)),
        ceiling_(p.parameters_.getParameter(zlp::POutputCeiling::kID)) {
        setTitle("Level history");
        setDescription("The gained input, the output and the gain reduction over the last seconds. "
                       "Drag up or down to change the gain; drag the ceiling line to move the ceiling.");
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setOpaque(true);
    }

    void HistoryView::setInsets(const float right, const float bottom) {
        inset_right_.setTarget(right);
        inset_bottom_.setTarget(bottom);
        if (!isShowing()) {
            inset_right_.snap(right);
            inset_bottom_.snap(bottom);
            layoutPlot();
        }
    }

    void HistoryView::resized() {
        layoutPlot();
    }

    void HistoryView::layoutPlot() {
        auto b = getLocalBounds().toFloat();
        b.removeFromRight(inset_right_.getValue() + kAxisWidth);
        b.removeFromTop(kPadTop);
        b.removeFromBottom(inset_bottom_.getValue() + kPadBottom);
        plot_ = b.withTrimmedLeft(1.f);
        const auto w = static_cast<size_t>(std::max(1.f, std::floor(plot_.getWidth())));
        px_input_.resize(w);
        px_output_.resize(w);
        px_reduction_.resize(w);
        px_loudness_.resize(w);
        // the pixel columns depend on the width: rebuild them before anything paints
        aggregate();
    }

    float HistoryView::yOfDB(const float db) const {
        const auto t = std::clamp(db / min_db_, 0.f, 1.f);
        return plot_.getY() + t * plot_.getHeight();
    }

    float HistoryView::dbOfY(const float y) const {
        return (y - plot_.getY()) / std::max(1.f, plot_.getHeight()) * min_db_;
    }

    void HistoryView::tick(const double dt, const size_t new_columns, const LoudnessReadings&) {
        auto& params = p_ref_.parameters_;
        auto& na = p_ref_.na_parameters_;
        const auto min_db = zlstate::PAnalyzerMinDB::getDBFromIndex(value(na, zlstate::PAnalyzerMinDB::kID));
        const auto seconds = zlstate::PAnalyzerTimeLength::kLength[static_cast<size_t>(
            std::clamp(index(na, zlstate::PAnalyzerTimeLength::kID), 0, 3))];
        const auto ceiling = value(params, zlp::POutputCeiling::kID);
        const auto gain = value(params, zlp::PInputGain::kID);
        const auto target = currentTarget(na).lufs;
        const auto bypassed = value(params, zlp::PBypass::kID) > .5f;
        const auto delta = value(params, zlp::PDelta::kID) > .5f;
        const auto true_peak = value(params, zlp::PTruePeak::kID) > .5f;

        const auto differs = [](const float a, const float b) { return std::abs(a - b) > 1e-6f; };
        auto changed = new_columns > 0;
        changed |= differs(min_db, min_db_) || differs(seconds, seconds_) || differs(ceiling, ceiling_db_) ||
                   differs(gain, gain_db_);
        changed |= target.has_value() != target_lufs_.has_value() ||
                   (target.has_value() && differs(*target, *target_lufs_));
        changed |= bypassed != bypassed_ || delta != delta_ || true_peak != true_peak_;
        min_db_ = min_db;
        seconds_ = seconds;
        ceiling_db_ = ceiling;
        gain_db_ = gain;
        target_lufs_ = target;
        bypassed_ = bypassed;
        delta_ = delta;
        true_peak_ = true_peak;

        const auto moved_right = inset_right_.update(static_cast<float>(dt));
        const auto moved_bottom = inset_bottom_.update(static_cast<float>(dt));
        if (moved_right || moved_bottom) {
            layoutPlot();
            changed = true;
        }
        if (changed) {
            aggregate();
            repaint();
        }
    }

    void HistoryView::aggregate() {
        const auto width = px_input_.size();
        const auto per_pixel = seconds_ * static_cast<float>(LevelFeed::kColumnsPerSecond) /
                               static_cast<float>(std::max<size_t>(1, width));
        const auto available = feed_.size();
        px_count_ = 0;
        for (size_t j = 0; j < width; ++j) {
            const auto from = static_cast<size_t>(std::floor(static_cast<float>(j) * per_pixel));
            const auto to = std::max(from + 1, static_cast<size_t>(std::floor(static_cast<float>(j + 1) * per_pixel)));
            if (from >= available) {
                break;
            }
            auto in = LevelFeed::kFloor, out = LevelFeed::kFloor, red = 0.f;
            for (auto i = from; i < std::min(to, available); ++i) {
                const auto& c = feed_.ago(i);
                in = std::max(in, c.input);
                out = std::max(out, c.output);
                red = std::min(red, c.reduction);
            }
            px_input_[j] = in;
            px_output_[j] = out;
            px_reduction_[j] = red;
            px_loudness_[j] = feed_.ago(from).loudness;
            px_count_ = j + 1;
        }
        // display ballistics, oldest to newest: peaks fall back at a fixed rate, so the waveform reads as an
        // envelope instead of 10 ms noise (the hover readout shows the same values)
        const auto seconds_per_px = seconds_ / static_cast<float>(std::max<size_t>(1, width));
        const auto level_fall = kLevelFallPerSecond * seconds_per_px;
        const auto reduction_rise = kReductionRisePerSecond * seconds_per_px;
        for (size_t j = px_count_; j-- > 1;) {
            px_input_[j - 1] = std::max(px_input_[j - 1], px_input_[j] - level_fall);
            px_output_[j - 1] = std::max(px_output_[j - 1], px_output_[j] - level_fall);
            px_reduction_[j - 1] = std::min(px_reduction_[j - 1], px_reduction_[j] + reduction_rise);
        }
    }

    void HistoryView::paintGrid(juce::Graphics& g) {
        const auto step = -min_db_ <= 12.f ? 2.f : -min_db_ <= 24.f ? 3.f : -min_db_ <= 36.f ? 6.f : 9.f;
        g.setFont(Fonts::get().font(type::kCaption));
        for (auto d = 0.f; d <= -min_db_ + .01f; d += step) {
            const auto y = std::round(yOfDB(-d)) + .5f;
            g.setColour(d == 0.f ? colour::kGridZero : colour::kGridMajor);
            g.fillRect(juce::Rectangle<float>(plot_.getX(), y - .5f, plot_.getWidth(), 1.f));
            g.setColour(colour::kTextTertiary.withMultipliedAlpha(.9f));
            const auto label = d == 0.f ? juce::String("0") : ui::format::kMinus + juce::String(static_cast<int>(d));
            g.drawText(label, juce::Rectangle<float>(plot_.getRight() + 4.f, y - 7.f, kAxisWidth - 10.f, 14.f),
                       juce::Justification::centredRight);
        }
        // one-second ticks
        const auto px_per_second = plot_.getWidth() / seconds_;
        g.setColour(colour::kGridMinor);
        for (int s = 1; s < static_cast<int>(seconds_); ++s) {
            const auto x = std::round(plot_.getRight() - static_cast<float>(s) * px_per_second) + .5f;
            g.fillRect(juce::Rectangle<float>(x - .5f, plot_.getY(), 1.f, plot_.getHeight()));
        }
    }

    void HistoryView::paint(juce::Graphics& g) {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(colour::kGraph);
        g.fillRect(bounds);
        paintGrid(g);

        const auto alpha = bypassed_ ? .4f : 1.f;
        const auto top = plot_.getY(), bottom = plot_.getBottom();
        const auto x_of = [&](const size_t j) { return plot_.getRight() - static_cast<float>(j); };
        const auto reduction_y = [&](const float db) {
            return top + std::clamp(db / min_db_, 0.f, 1.f) * plot_.getHeight();
        };

        // loudness target: a band of +-1 LU and the target itself
        if (target_lufs_.has_value() && *target_lufs_ > min_db_) {
            const auto y0 = yOfDB(*target_lufs_ + 1.f), y1 = yOfDB(*target_lufs_ - 1.f);
            g.setColour(colour::kLoudness.withAlpha(.055f));
            g.fillRect(juce::Rectangle<float>(plot_.getX(), y0, plot_.getWidth(), y1 - y0));
            const auto y = std::round(yOfDB(*target_lufs_)) + .5f;
            const float dashes[] = {3.f, 4.f};
            g.setColour(colour::kLoudness.withAlpha(.35f));
            g.drawDashedLine({plot_.getX(), y, plot_.getRight(), y}, dashes, 2, 1.f);
            g.setFont(Fonts::get().font(type::kCaption, Fonts::kSemiBold).withExtraKerningFactor(.06f));
            g.setColour(colour::kLoudness.withAlpha(.7f));
            g.drawText("TARGET " + level(*target_lufs_, 0) + " LUFS",
                       juce::Rectangle<float>(plot_.getX() + 10.f, y - 15.f, 160.f, 13.f),
                       juce::Justification::centredLeft);
        }

        if (px_count_ > 1) {
            g.saveState();
            g.reduceClipRegion(plot_.toNearestInt());
            // input: the gained signal as a faint haze above the output
            juce::Path input;
            input.startNewSubPath(x_of(0), bottom);
            for (size_t j = 0; j < px_count_; ++j) {
                input.lineTo(x_of(j), yOfDB(px_input_[j]));
            }
            input.lineTo(x_of(px_count_ - 1), bottom);
            input.closeSubPath();
            g.setGradientFill(juce::ColourGradient(colour::kInput.withAlpha(.20f * alpha), 0.f, top,
                                                   colour::kInput.withAlpha(.03f * alpha), 0.f, bottom, false));
            g.fillPath(input);

            // output: a steel body under a crisp light edge
            const auto out_colour = delta_ ? colour::kWarning : colour::kOutput;
            const auto body = delta_ ? colour::kWarning : colour::kOutputBody;
            juce::Path out_line, out_fill;
            out_fill.startNewSubPath(x_of(0), bottom);
            for (size_t j = 0; j < px_count_; ++j) {
                const auto y = yOfDB(px_output_[j]);
                j == 0 ? out_line.startNewSubPath(x_of(j), y) : out_line.lineTo(x_of(j), y);
                out_fill.lineTo(x_of(j), y);
            }
            out_fill.lineTo(x_of(px_count_ - 1), bottom);
            out_fill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(body.withAlpha(.42f * alpha), 0.f, top,
                                                   body.withAlpha(.05f * alpha), 0.f, bottom, false));
            g.fillPath(out_fill);
            g.setColour(out_colour.withAlpha(.78f * alpha));
            g.strokePath(out_line, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved));

            // gain reduction hangs from the top edge
            juce::Path red_line, red_fill;
            red_fill.startNewSubPath(x_of(0), top);
            for (size_t j = 0; j < px_count_; ++j) {
                const auto y = reduction_y(px_reduction_[j]);
                j == 0 ? red_line.startNewSubPath(x_of(j), y) : red_line.lineTo(x_of(j), y);
                red_fill.lineTo(x_of(j), y);
            }
            red_fill.lineTo(x_of(px_count_ - 1), top);
            red_fill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(colour::kReduction.withAlpha(.30f * alpha), 0.f, top,
                                                   colour::kReduction.withAlpha(.04f * alpha), 0.f, top + 90.f,
                                                   false));
            g.fillPath(red_fill);
            g.setColour(colour::kReduction.withAlpha(.95f * alpha));
            g.strokePath(red_line, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));

            // short-term loudness
            juce::Path loud;
            bool drawing = false;
            for (size_t j = 0; j < px_count_; ++j) {
                if (px_loudness_[j] <= min_db_ || px_loudness_[j] <= LoudnessReadings::kNoLevel) {
                    drawing = false;
                    continue;
                }
                const auto y = yOfDB(px_loudness_[j]);
                drawing ? loud.lineTo(x_of(j), y) : loud.startNewSubPath(x_of(j), y);
                drawing = true;
            }
            g.setColour(colour::kLoudness.withAlpha(.9f * alpha));
            g.strokePath(loud, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
            g.restoreState();
        }

        paintCeiling(g);
        paintLegend(g);
        if (bypassed_) {
            g.setFont(Fonts::get().font(type::kSmall, Fonts::kSemiBold).withExtraKerningFactor(.16f));
            g.setColour(colour::kWarning.withAlpha(.85f));
            g.drawText("BYPASSED", plot_.withHeight(28.f).translated(0.f, plot_.getHeight() * .35f),
                       juce::Justification::centred);
        }
        if (drag_ == Drag::kGain) {
            paintDragTag(g);
        } else if (drag_ == Drag::kNone) {
            paintHover(g);
        }
    }

    void HistoryView::paintCeiling(juce::Graphics& g) const {
        const auto y = std::round(yOfDB(ceiling_db_)) + .5f;
        const auto hot = ceiling_hot_ || drag_ == Drag::kCeiling;
        const auto accent = colour::kAccent;
        if (hot) {
            g.setColour(accent);
            g.fillRect(juce::Rectangle<float>(plot_.getX(), y - .75f, plot_.getWidth(), 1.5f));
        } else {
            const float dashes[] = {5.f, 4.f};
            g.setColour(accent.withAlpha(.7f));
            g.drawDashedLine({plot_.getX(), y, plot_.getRight(), y}, dashes, 2, 1.f);
        }
        tag(g, "CEILING " + level(ceiling_db_, 1) + (true_peak_ ? " dBTP" : " dBFS"), plot_.getRight() - 10.f,
            std::max(y, plot_.getY() + 10.f) + (y < plot_.getY() + 24.f ? 12.f : -12.f), accent, hot);
    }

    void HistoryView::paintLegend(juce::Graphics& g) const {
        const auto font = Fonts::get().font(type::kCaption);
        g.setFont(font);
        auto x = plot_.getX() + 12.f;
        const auto y = 15.f;
        const auto item = [&](const juce::Colour c, const juce::String& name, const bool fill) {
            if (fill) {
                g.setColour(c);
                g.fillRoundedRectangle(juce::Rectangle<float>(x, y - 4.f, 10.f, 8.f), 1.5f);
            } else {
                g.setColour(c);
                g.fillRoundedRectangle(juce::Rectangle<float>(x, y - .75f, 10.f, 1.5f), .75f);
            }
            g.setColour(colour::kTextSecondary.withMultipliedAlpha(.85f));
            const auto w = juce::GlyphArrangement::getStringWidth(font, name);
            g.drawText(name, juce::Rectangle<float>(x + 15.f, y - 7.f, w + 2.f, 14.f), juce::Justification::centredLeft);
            x += 15.f + w + 16.f;
        };
        item(colour::kInput.withAlpha(.6f), "Input", true);
        item(delta_ ? colour::kWarning : colour::kOutput.withAlpha(.85f), delta_ ? "Delta" : "Output", false);
        item(colour::kReduction, "Gain reduction", false);
        item(colour::kLoudness, "Short-term loudness", false);

        g.setColour(colour::kTextTertiary.withMultipliedAlpha(.8f));
        g.drawText("Drag to set the gain  " + juce::String::charToString(static_cast<juce::juce_wchar>(0x00B7)) +
                   "  Shift for fine",
                   juce::Rectangle<float>(x + 8.f, y - 7.f, std::max(0.f, plot_.getRight() - x - 8.f), 14.f),
                   juce::Justification::centredRight);
    }

    void HistoryView::paintHover(juce::Graphics& g) const {
        if (!hover_.has_value() || ceiling_hot_ || !plot_.contains(*hover_)) {
            return;
        }
        const auto j = static_cast<size_t>(std::max(0.f, std::round(plot_.getRight() - hover_->x)));
        if (j >= px_count_) {
            return;
        }
        const auto x = std::round(plot_.getRight() - static_cast<float>(j)) + .5f;
        g.setColour(colour::kTextPrimary.withAlpha(.22f));
        g.fillRect(juce::Rectangle<float>(x - .5f, plot_.getY(), 1.f, plot_.getHeight()));

        const auto seconds = static_cast<float>(j) / plot_.getWidth() * seconds_;
        const std::array<std::pair<juce::String, juce::String>, 5> rows{
            {
                {ui::format::kMinus + juce::String(seconds, 2) + " s", ""},
                {"In", level(px_input_[j], 1) + " dB"},
                {"Out", level(px_output_[j], 1) + " dB"},
                {"GR", level(px_reduction_[j], 1) + " dB"},
                {"Short-term", px_loudness_[j] > LoudnessReadings::kNoLevel ? level(px_loudness_[j], 1) + " LUFS"
                                                                            : juce::String("--")},
            }
        };
        constexpr float w = 150.f, row_h = 16.f;
        const auto h = 10.f + row_h * static_cast<float>(rows.size());
        auto bx = x + 12.f;
        if (bx + w > plot_.getRight() - 6.f) {
            bx = x - 12.f - w;
        }
        const auto by = std::clamp(hover_->y - h * .5f, plot_.getY() + 4.f, plot_.getBottom() - h - 4.f);
        const juce::Rectangle<float> box(bx, by, w, h);
        g.setColour(colour::kOverlay);
        g.fillRoundedRectangle(box, size::kRadius);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(box.reduced(.5f), size::kRadius, 1.f);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto r = juce::Rectangle<float>(bx + 9.f, by + 5.f + row_h * static_cast<float>(i), w - 18.f, row_h);
            g.setFont(Fonts::get().font(type::kSmall));
            g.setColour(i == 0 ? colour::kTextSecondary : colour::kTextTertiary);
            g.drawText(rows[i].first, r, juce::Justification::centredLeft);
            g.setColour(colour::kTextPrimary);
            Fonts::get().drawTabular(g, rows[i].second, r, juce::Justification::centredRight, type::kSmall);
        }
        // markers on the curves
        g.setColour(colour::kReduction);
        g.fillEllipse(juce::Rectangle<float>(6.f, 6.f).withCentre(
            {x, plot_.getY() + std::clamp(px_reduction_[j] / min_db_, 0.f, 1.f) * plot_.getHeight()}));
        g.setColour(colour::kOutput);
        g.fillEllipse(juce::Rectangle<float>(6.f, 6.f).withCentre({x, yOfDB(px_output_[j])}));
    }

    void HistoryView::paintDragTag(juce::Graphics& g) const {
        if (!hover_.has_value()) {
            return;
        }
        tag(g, "GAIN " + ui::format::gain(gain_db_), std::min(hover_->x + 80.f, plot_.getRight() - 4.f),
            std::clamp(hover_->y - 22.f, plot_.getY() + 10.f, plot_.getBottom() - 10.f), colour::kAccent, true);
    }

    bool HistoryView::nearCeiling(const juce::Point<float> p) const {
        return p.x >= plot_.getX() && p.x <= plot_.getRight() + kAxisWidth &&
               std::abs(p.y - yOfDB(ceiling_db_)) <= kCeilingGrab;
    }

    void HistoryView::mouseMove(const juce::MouseEvent& e) {
        hover_ = e.position;
        ceiling_hot_ = nearCeiling(e.position);
        setMouseCursor(ceiling_hot_ ? juce::MouseCursor::UpDownResizeCursor
                                    : plot_.contains(e.position) ? juce::MouseCursor::CrosshairCursor
                                                                 : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void HistoryView::mouseExit(const juce::MouseEvent&) {
        hover_.reset();
        ceiling_hot_ = false;
        repaint();
    }

    int HistoryView::getHoveredParameterIndex() const {
        return (ceiling_hot_ ? ceiling_ : gain_)->getParameterIndex();
    }

    void HistoryView::mouseDown(const juce::MouseEvent& e) {
        if (e.mods.isPopupMenu()) {
            return;
        }
        if (nearCeiling(e.position)) {
            if (e.mods.isCommandDown()) {
                editor_.setNow({{ceiling_, zlp::POutputCeiling::kDefaultV}}, "Reset Ceiling");
                return;
            }
            drag_ = Drag::kCeiling;
            std::array<juce::RangedAudioParameter*, 1> paras{ceiling_};
            editor_.beginEdit(paras, "Change Ceiling");
        } else if (plot_.contains(e.position)) {
            drag_ = Drag::kGain;
            drag_last_y_ = e.position.y;
            drag_gain_ = gain_db_;
            std::array<juce::RangedAudioParameter*, 1> paras{gain_};
            editor_.beginEdit(paras, "Change Gain");
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        }
        repaint();
    }

    void HistoryView::mouseDrag(const juce::MouseEvent& e) {
        hover_ = e.position;
        if (drag_ == Drag::kCeiling) {
            const auto db = std::round(std::clamp(dbOfY(e.position.y), -12.f, 0.f) * 10.f) / 10.f;
            editor_.setPlain(ceiling_, db);
        } else if (drag_ == Drag::kGain) {
            // one pixel moves the gain as far as one pixel of the display's scale
            const auto per_pixel = -min_db_ / std::max(1.f, plot_.getHeight());
            const auto fine = e.mods.isShiftDown() ? .15f : 1.f;
            drag_gain_ += (drag_last_y_ - e.position.y) * per_pixel * fine;
            drag_last_y_ = e.position.y;
            drag_gain_ = std::clamp(drag_gain_, zlp::PInputGain::kRange.start, zlp::PInputGain::kRange.end);
            editor_.setPlain(gain_, std::round(drag_gain_ * 10.f) / 10.f);
        }
        repaint();
    }

    void HistoryView::mouseUp(const juce::MouseEvent& e) {
        if (drag_ != Drag::kNone) {
            editor_.endEdit();
            drag_ = Drag::kNone;
            mouseMove(e);
        }
    }

    void HistoryView::mouseDoubleClick(const juce::MouseEvent& e) {
        if (nearCeiling(e.position)) {
            editor_.setNow({{ceiling_, zlp::POutputCeiling::kDefaultV}}, "Reset Ceiling");
        }
    }

    bool HistoryView::keyPressed(const juce::KeyPress& key) {
        const auto step = key.getModifiers().isShiftDown() ? .1f : .5f;
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
            const auto next = gain_db_ + (key == juce::KeyPress::upKey ? step : -step);
            editor_.setNow({{gain_, next}}, "Change Gain");
            return true;
        }
        return false;
    }
}
