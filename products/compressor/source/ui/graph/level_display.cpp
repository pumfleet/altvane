// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "level_display.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "graph_helpers.hpp"
#include "../model/formats.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;

    LevelDisplay::LevelDisplay(CompBridge& bridge, LevelHistory& history) : bridge_(bridge), history_(history) {
        setOpaque(true);
        setTitle("Level display");
        setDescription("Input, output and gain change over time. Drag the threshold line to set the threshold.");
        setComponentID(zlp::PThreshold::kID);
    }

    LevelDisplay::~LevelDisplay() {
        timerCallback();
    }

    void LevelDisplay::setPlotRight(const float x) {
        plot_right_ = x;
    }

    int LevelDisplay::getPlotWidth() const {
        return std::max(juce::roundToInt(plot_right_), 1);
    }

    void LevelDisplay::resized() {
        if (plot_right_ <= 0.f) {
            plot_right_ = static_cast<float>(getWidth()) - 40.f;
        }
    }

    float LevelDisplay::yOf(const float db) const {
        const auto h = static_cast<float>(getHeight()) - kPadTop - kPadBottom;
        return kPadTop + (-db / scale_db_) * h;
    }

    float LevelDisplay::dbOf(const float y) const {
        const auto h = static_cast<float>(getHeight()) - kPadTop - kPadBottom;
        return -(y - kPadTop) / std::max(h, 1.f) * scale_db_;
    }

    float LevelDisplay::gainY(const float db) const {
        const auto h = static_cast<float>(getHeight()) - kPadTop - kPadBottom;
        return kPadTop + std::min(std::abs(db) / scale_db_, 1.f) * h;
    }

    void LevelDisplay::tick() {
        scale_db_ = bridge_.getScaleDB();
        buildPaths();
        repaint();
    }

    void LevelDisplay::buildPaths() {
        input_path_.clear();
        output_path_.clear();
        gain_fill_.clear();
        gain_line_.clear();
        const auto n = std::min(history_.size(), static_cast<size_t>(getPlotWidth()) + 2);
        if (n < 2) {
            return;
        }
        const auto bottom = static_cast<float>(getHeight()) - kPadBottom;
        const auto clampY = [&](const float db) { return std::clamp(yOf(db), kPadTop, bottom); };
        // one column per pixel; the newest column slides in as it fills
        const auto x0 = plot_right_ - history_.getPhase();
        const auto colX = [&](const size_t i) { return x0 - static_cast<float>(i); };

        input_path_.preallocateSpace(static_cast<int>(n) * 3 + 8);
        output_path_.preallocateSpace(static_cast<int>(n) * 3 + 8);
        gain_line_.preallocateSpace(static_cast<int>(n) * 3 + 8);

        input_path_.startNewSubPath(colX(0), bottom);
        gain_fill_.startNewSubPath(colX(0), kPadTop);
        auto gain_drawing = false;
        for (size_t i = 0; i < n; ++i) {
            const auto& c = history_.get(i);
            const auto x = colX(i);
            input_path_.lineTo(x, clampY(c.input));
            const auto oy = clampY(c.output);
            const auto gy = gainY(c.gain);
            if (i == 0) {
                output_path_.startNewSubPath(x, oy);
            } else {
                output_path_.lineTo(x, oy);
            }
            // no line along the top edge while nothing is being changed
            const auto acting = std::abs(c.gain) > .05f;
            if (acting && !gain_drawing) {
                gain_line_.startNewSubPath(i == 0 ? x : x + 1.f, gainY(i == 0 ? c.gain : history_.get(i - 1).gain));
                gain_line_.lineTo(x, gy);
            } else if (acting || gain_drawing) {
                gain_line_.lineTo(x, gy);
            }
            gain_drawing = acting;
            gain_fill_.lineTo(x, gy);
        }
        input_path_.lineTo(colX(n - 1), bottom);
        input_path_.closeSubPath();
        gain_fill_.lineTo(colX(n - 1), kPadTop);
        gain_fill_.closeSubPath();
    }

    void LevelDisplay::paint(juce::Graphics& g) {
        g.fillAll(colour::kGraph);
        drawGrid(g);

        const auto bottom = static_cast<float>(getHeight()) - kPadBottom;
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(juce::Rectangle<float>(0.f, 0.f, plot_right_, static_cast<float>(getHeight()))
                .toNearestInt());
            g.setGradientFill(juce::ColourGradient(colour::kInputFill.withAlpha(.30f), 0.f, kPadTop,
                                                   colour::kInputFill.withAlpha(.10f), 0.f, bottom, false));
            g.fillPath(input_path_);

            const auto delta = bridge_.getBool(zlp::PCompDelta::kID);
            g.setColour(delta ? colour::kWarning.withAlpha(.9f) : colour::kOutputLine.withAlpha(.82f));
            g.strokePath(output_path_, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved,
                                                            juce::PathStrokeType::rounded));

            const auto gain_colour = bridge_.isUpward() ? colour::kUpward : colour::kReduction;
            g.setGradientFill(juce::ColourGradient(gain_colour.withAlpha(.26f), 0.f, kPadTop,
                                                   gain_colour.withAlpha(.04f), 0.f, kPadTop + 120.f, false));
            g.fillPath(gain_fill_);
            g.setColour(gain_colour);
            g.strokePath(gain_line_, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
        }
        drawThreshold(g);
        drawLegend(g);
        drawHover(g);
    }

    void LevelDisplay::drawGrid(juce::Graphics& g) const {
        const auto step = graph::gridStep(scale_db_);
        g.setFont(graph::labelFont());
        const auto label_x = plot_right_ + 8.f;
        for (float d = 0.f; d <= scale_db_ + .01f; d += step) {
            const auto y = std::round(yOf(-d)) + .5f;
            g.setColour(d == 0.f ? juce::Colour{0x24FFFFFF} : juce::Colour{0x0EFFFFFF});
            g.fillRect(juce::Rectangle<float>(0.f, y - .5f, plot_right_, 1.f));
            g.setColour(juce::Colour{0x57FFFFFF});
            g.drawText(graph::gridDB(-d), juce::Rectangle<float>(label_x, y - 8.f, 32.f, 16.f),
                       juce::Justification::centredLeft);
        }
        // one faint line per second
        const auto per_second = 1.f / std::max(history_.getSecondsPerColumn(), 1e-4f);
        g.setColour(juce::Colour{0x08FFFFFF});
        for (auto x = plot_right_ - per_second; x > 0.f; x -= per_second) {
            g.fillRect(juce::Rectangle<float>(std::round(x), kPadTop, 1.f,
                                              static_cast<float>(getHeight()) - kPadTop - kPadBottom));
        }
    }

    void LevelDisplay::drawThreshold(juce::Graphics& g) const {
        const auto threshold = bridge_.get(zlp::PThreshold::kID);
        if (threshold <= -scale_db_) {
            return;
        }
        const auto y = std::round(yOf(threshold)) + .5f;
        const auto hot = threshold_hover_ || dragging_threshold_;
        g.setColour(colour::kAccent.withAlpha(hot ? 1.f : .7f));
        if (hot) {
            g.fillRect(juce::Rectangle<float>(0.f, y - .75f, plot_right_, 1.5f));
        } else {
            constexpr float dashes[] = {5.f, 4.f};
            g.drawDashedLine(juce::Line<float>(0.f, y, plot_right_, y), dashes, 2, 1.f);
        }
        const auto text = "Threshold " + formats::db(threshold);
        const auto font = Fonts::get().font(type::kCaption, Fonts::kSemiBold);
        const auto w = juce::GlyphArrangement::getStringWidth(font, text) + 14.f;
        const auto tag = juce::Rectangle<float>(plot_right_ - w - 10.f, y - 9.f, w, 18.f);
        g.setColour(hot ? colour::kAccent : juce::Colour{0xEB15181C});
        g.fillRoundedRectangle(tag, size::kRadiusSmall);
        g.setColour(colour::kAccent.withAlpha(.6f));
        g.drawRoundedRectangle(tag.reduced(.5f), size::kRadiusSmall, 1.f);
        g.setColour(hot ? colour::kWindow : colour::kAccent);
        g.setFont(font);
        g.drawText(text, tag.reduced(7.f, 0.f), juce::Justification::centredLeft);
    }

    void LevelDisplay::drawLegend(juce::Graphics& g) const {
        const auto upward = bridge_.isUpward();
        const auto delta = bridge_.getBool(zlp::PCompDelta::kID);
        struct Item {
            juce::Colour colour;
            juce::String name;
            bool fill;
        };
        const Item items[] = {
            {colour::kInputFill.withAlpha(.55f), "Input", true},
            {delta ? colour::kWarning : colour::kOutputLine.withAlpha(.85f), delta ? "Delta" : "Output", false},
            {upward ? colour::kUpward : colour::kReduction, upward ? "Gain" : "Gain reduction", false},
        };
        g.setFont(graph::labelFont());
        auto total = 0.f;
        for (const auto& item : items) {
            total += 15.f + juce::GlyphArrangement::getStringWidth(graph::labelFont(), item.name) + 14.f;
        }
        const auto y = kPadTop + 10.f;
        g.setColour(colour::kGraph.withAlpha(.82f));
        g.fillRoundedRectangle(juce::Rectangle<float>(6.f, y - 10.f, total - 2.f, 20.f), size::kRadiusSmall);
        auto x = 12.f;
        for (const auto& item : items) {
            g.setColour(item.colour);
            if (item.fill) {
                g.fillRect(juce::Rectangle<float>(x, y - 4.f, 10.f, 8.f));
            } else {
                g.fillRect(juce::Rectangle<float>(x, y - .75f, 10.f, 1.5f));
            }
            const auto w = juce::GlyphArrangement::getStringWidth(graph::labelFont(), item.name);
            g.setColour(juce::Colour{0x80FFFFFF});
            g.drawText(item.name, juce::Rectangle<float>(x + 15.f, y - 8.f, w + 4.f, 16.f),
                       juce::Justification::centredLeft);
            x += 15.f + w + 14.f;
        }
    }

    void LevelDisplay::drawHover(juce::Graphics& g) const {
        if (!hover_.has_value() || dragging_threshold_ || threshold_hover_) {
            return;
        }
        const auto i = juce::roundToInt(plot_right_ - history_.getPhase() - hover_->x);
        if (i < 0 || static_cast<size_t>(i) >= history_.size()) {
            return;
        }
        const auto& c = history_.get(static_cast<size_t>(i));
        const auto x = std::round(plot_right_ - history_.getPhase() - static_cast<float>(i)) + .5f;
        const auto bottom = static_cast<float>(getHeight()) - kPadBottom;
        g.setColour(juce::Colour{0x38FFFFFF});
        g.fillRect(juce::Rectangle<float>(x - .5f, kPadTop, 1.f, bottom - kPadTop));

        const auto upward = bridge_.isUpward();
        const auto gain_colour = upward ? colour::kUpward : colour::kReduction;
        const auto seconds = static_cast<float>(i) * history_.getSecondsPerColumn();
        const auto level = [](const float db) {
            return db <= LevelHistory::kFloorDB + .5f ? ui::format::kMinus + juce::String::fromUTF8("\xe2\x88\x9e")
                                                       : formats::db(db);
        };
        graph::drawReadout(g, {x, hover_->y}, {
                               {ui::format::kMinus + juce::String(seconds, 2) + " s", ""},
                               {"In", level(c.input)},
                               {"Out", level(c.output)},
                               {upward ? "Gain" : "GR", formats::db(c.gain)},
                           }, juce::Rectangle<float>(0.f, 0.f, plot_right_, static_cast<float>(getHeight())));
        g.setColour(gain_colour);
        g.fillEllipse(juce::Rectangle<float>(6.f, 6.f).withCentre({x, gainY(c.gain)}));
        g.setColour(colour::kOutputLine);
        g.fillEllipse(juce::Rectangle<float>(6.f, 6.f).withCentre({x, std::clamp(yOf(c.output), kPadTop, bottom)}));
    }

    bool LevelDisplay::isNearThreshold(const juce::Point<float> p) const {
        const auto threshold = bridge_.get(zlp::PThreshold::kID);
        return p.x < plot_right_ && threshold > -scale_db_ && std::abs(p.y - yOf(threshold)) < 6.f;
    }

    void LevelDisplay::mouseMove(const juce::MouseEvent& e) {
        hover_ = e.position.x < plot_right_ ? std::optional(e.position) : std::nullopt;
        threshold_hover_ = isNearThreshold(e.position);
        setMouseCursor(threshold_hover_ ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }

    void LevelDisplay::mouseExit(const juce::MouseEvent&) {
        hover_.reset();
        threshold_hover_ = false;
        repaint();
    }

    void LevelDisplay::mouseDown(const juce::MouseEvent& e) {
        if (!isNearThreshold(e.position)) {
            return;
        }
        timerCallback();
        auto* para = bridge_.para(zlp::PThreshold::kID);
        if (e.mods.isPopupMenu()) {
            return;
        }
        if (e.mods.isCommandDown()) {
            bridge_.set(zlp::PThreshold::kID, para->convertFrom0to1(para->getDefaultValue()), "Threshold");
            return;
        }
        juce::RangedAudioParameter* paras[] = {para};
        bridge_.getEditor().beginEdit(paras, "Change Threshold");
        dragging_threshold_ = true;
    }

    void LevelDisplay::mouseDrag(const juce::MouseEvent& e) {
        if (!dragging_threshold_) {
            return;
        }
        const auto db = std::clamp(std::round(dbOf(e.position.y) * 10.f) / 10.f, -100.f, 0.f);
        bridge_.getEditor().setPlain(bridge_.para(zlp::PThreshold::kID), db);
        repaint();
    }

    void LevelDisplay::mouseUp(const juce::MouseEvent& e) {
        if (dragging_threshold_) {
            dragging_threshold_ = false;
            bridge_.getEditor().endEdit();
            mouseMove(e);
        }
    }

    void LevelDisplay::mouseDoubleClick(const juce::MouseEvent& e) {
        if (isNearThreshold(e.position)) {
            auto* para = bridge_.para(zlp::PThreshold::kID);
            bridge_.set(zlp::PThreshold::kID, para->convertFrom0to1(para->getDefaultValue()), "Threshold");
        }
    }

    void LevelDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
        // wheel: threshold; with Cmd: ratio; a burst of wheel events is one undo step
        const auto id = juce::String(e.mods.isCommandDown() ? zlp::PRatio::kID : zlp::PThreshold::kID);
        auto* para = bridge_.para(id);
        const auto delta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs(delta) < 1e-6f || dragging_threshold_) {
            return;
        }
        if (wheel_id_ != id) {
            if (wheel_id_.isNotEmpty()) {
                bridge_.getEditor().endEdit();
            }
            wheel_id_ = id;
            juce::RangedAudioParameter* paras[] = {para};
            bridge_.getEditor().beginEdit(paras, id == zlp::PRatio::kID ? "Change Ratio" : "Change Threshold");
        }
        const auto fine = e.mods.isShiftDown() ? .2f : 1.f;
        const auto norm = std::clamp(para->getValue() + delta * (wheel.isReversed ? -1.f : 1.f) * .05f * fine,
                                     0.f, 1.f);
        bridge_.getEditor().setPlain(para, para->convertFrom0to1(norm));
        startTimer(450);
    }

    void LevelDisplay::timerCallback() {
        stopTimer();
        if (wheel_id_.isNotEmpty()) {
            wheel_id_.clear();
            bridge_.getEditor().endEdit();
        }
    }
}
