// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "history_view.hpp"

#include <altvane_ui/util/value_format.hpp>

#include "theme/leveler_tokens.hpp"

namespace altvane::leveler {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kLevelLow = -60.f, kLevelHigh = 0.f;
        constexpr float kLabelWidth = 30.f;

        juce::Colour stateColour(const dsp::HopState state) {
            switch (state) {
                case dsp::HopState::kVoiced: return colour::kVoiced.withAlpha(.55f);
                case dsp::HopState::kUnvoiced: return colour::kUnvoiced.withAlpha(.55f);
                case dsp::HopState::kSilent: return juce::Colours::transparentBlack;
            }
            return juce::Colours::transparentBlack;
        }
    }

    HistoryView::HistoryView(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        setTitle("Level history");
        setDescription("Input, output and music levels over the last seconds, with the target and the sensitivity floor");
        level_.setRange(kLevelLow, kLevelHigh);
        setOpaque(true);
    }

    void HistoryView::clear() {
        dsp::Column column;
        while (p_ref_.getEngine().popColumn(column)) {
        }
        ring_.clear();
        repaint();
    }

    void HistoryView::tick() {
        auto& engine = p_ref_.getEngine();
        dsp::Column column;
        auto any = false;
        while (engine.popColumn(column)) {
            ring_.push(column);
            any = true;
        }
        const auto rate = p_ref_.getSampleRate();
        if (rate > 0.0) {
            time_.setSpan(static_cast<size_t>(motion::kHistorySeconds * rate / engine.getHopSamples()));
        }
        // the lines and the gain lane follow the controls even while no audio arrives (a host stopped and suspended)
        const auto& t = p_ref_.parameters_;
        const std::array<float, 6> look{
            t.getRawParameterValue(params::kTarget)->load(), t.getRawParameterValue(params::kSensitivity)->load(),
            t.getRawParameterValue(params::kRangeMin)->load(), t.getRawParameterValue(params::kRangeMax)->load(),
            t.getRawParameterValue(params::kMusic)->load(), static_cast<float>(p_ref_.getKeyChannels())
        };
        if (any || look != look_) {
            look_ = look;
            repaint();
        }
    }

    void HistoryView::resized() {
        plot_ = getLocalBounds().toFloat();
        auto b = plot_.reduced(0.f, size::kS2);
        gain_area_ = b.removeFromBottom(std::round(b.getHeight() * size::kGainLaneShare));
        strip_area_ = b.removeFromBottom(size::kStripHeight);
        level_area_ = b;
        time_.setBounds(plot_.getX() + kLabelWidth, plot_.getWidth() - kLabelWidth - size::kS2);
        level_.setBounds(level_area_.getY() + size::kS1, level_area_.getHeight() - size::kS2);
        gain_.setBounds(gain_area_.getY() + size::kS2, gain_area_.getHeight() - size::kS3);
    }

    float HistoryView::targetDb() const {
        return p_ref_.parameters_.getRawParameterValue(params::kTarget)->load();
    }

    float HistoryView::floorDb() const {
        return targetDb() - 24.f - p_ref_.parameters_.getRawParameterValue(params::kSensitivity)->load();
    }

    float HistoryView::gainScale() const {
        const auto& t = p_ref_.parameters_;
        const auto extent = std::max(std::abs(t.getRawParameterValue(params::kRangeMin)->load()),
                                     std::abs(t.getRawParameterValue(params::kRangeMax)->load()));
        return extent > 12.f ? 24.f : 12.f;
    }

    void HistoryView::paint(juce::Graphics& g) {
        g.fillAll(colour::kGraph);
        const auto columns = ring_.size();
        const auto labels = juce::Rectangle<float>(plot_.getX() + size::kS2, plot_.getY(), kLabelWidth, plot_.getHeight());

        // levels
        ui::drawLevelGrid(g, level_, level_area_, 6.f, labels);
        const auto input = ui::traceHistory(time_, level_, columns, [this](const size_t a) { return ring_.at(a).in_db; },
                                            true, true);
        g.setColour(colour::kInput.withAlpha(.28f));
        g.fillPath(input);
        if (p_ref_.getKeyChannels() > 0) {
            const auto key = ui::traceHistory(time_, level_, columns, [this](const size_t a) { return ring_.at(a).key_db; },
                                              false, false);
            g.setColour(colour::kKey.withAlpha(.8f));
            g.strokePath(key, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved));
        }
        const auto output = ui::traceHistory(time_, level_, columns, [this](const size_t a) { return ring_.at(a).out_db; },
                                             true, false);
        g.setColour(colour::kOutput.withAlpha(.9f));
        g.strokePath(output, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));

        // the target the music key moves, then the target and floor themselves
        const auto target = targetDb();
        const auto music = p_ref_.parameters_.getRawParameterValue(params::kMusic)->load();
        if (music > .005f && p_ref_.getKeyChannels() > 0) {
            const auto effective = ui::traceHistory(time_, level_, columns,
                                                    [this](const size_t a) { return ring_.at(a).target_db; }, false, false);
            g.setColour(colour::kAccent.withAlpha(.45f));
            g.strokePath(effective, juce::PathStrokeType(1.f));
        }
        const auto line_x = time_.getX();
        const auto line_w = time_.getWidth();
        auto drawLine = [&](const float db, const juce::Colour c, const float thickness, const juce::String& caption,
                            const bool dashed) {
            const auto y = level_.dbToY(db);
            g.setColour(c);
            if (dashed) {
                const float dashes[] = {4.f, 4.f};
                g.drawDashedLine(juce::Line<float>(line_x, y, line_x + line_w, y), dashes, 2, thickness);
            } else {
                g.fillRect(juce::Rectangle<float>(line_x, y - thickness * .5f, line_w, thickness));
            }
            g.setFont(captionFont());
            const auto text = caption + "  " + params::formatDb(db);
            const auto w = juce::GlyphArrangement::getStringWidth(captionFont(), text) + size::kS2;
            g.drawText(text, juce::Rectangle<float>(line_x + line_w - w, y - 15.f, w, 13.f), juce::Justification::centredRight);
        };
        const auto floor = floorDb();
        drawLine(floor, (hover_line_ == Line::kFloor || drag_line_ == Line::kFloor ? colour::kTextSecondary : colour::kTextTertiary),
                 1.f, "FLOOR", true);
        drawLine(target, colour::kAccent.withAlpha(hover_line_ == Line::kTarget || drag_line_ == Line::kTarget ? 1.f : .8f),
                 1.5f, "TARGET", false);

        // what the detector heard
        for (size_t age = 0; age < std::min(columns, time_.getSpan()); ++age) {
            const auto c = stateColour(ring_.at(age).state);
            if (!c.isTransparent()) {
                const auto x = std::floor(time_.ageToX(static_cast<float>(age)));
                g.setColour(c);
                g.fillRect(juce::Rectangle<float>(x, strip_area_.getY() + 1.f, 1.f, strip_area_.getHeight() - 2.f));
            }
        }

        // gain lane: the range, 0 dB and the gain
        const auto scale = gainScale();
        gain_.setRange(-scale, scale);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(gain_area_.getX(), gain_area_.getY(), gain_area_.getWidth(), 1.f));
        const auto& t = p_ref_.parameters_;
        const auto lo = gain_.dbToY(t.getRawParameterValue(params::kRangeMin)->load());
        const auto hi = gain_.dbToY(t.getRawParameterValue(params::kRangeMax)->load());
        g.setColour(colour::kAccent.withAlpha(.05f));
        g.fillRect(juce::Rectangle<float>(line_x, std::min(lo, hi), line_w, std::abs(lo - hi)));
        g.setColour(colour::kGridZero);
        g.fillRect(juce::Rectangle<float>(line_x, std::round(gain_.dbToY(0.f)), line_w, 1.f));
        for (const auto db : {-scale, scale}) {
            const auto text = (db < 0.f ? ui::format::kMinus : juce::String("+")) + juce::String(juce::roundToInt(std::abs(db)));
            g.setColour(colour::kTextTertiary);
            Fonts::get().drawTabular(g, text, labels.withY(gain_.dbToY(db) - 7.f).withHeight(14.f),
                                     juce::Justification::centredLeft, type::kCaption);
        }
        g.setFont(captionFont());
        g.drawText("GAIN", labels.withY(gain_.dbToY(0.f) - 7.f).withHeight(14.f), juce::Justification::centredLeft);
        const auto gain = ui::traceHistory(time_, gain_, columns, [this](const size_t a) { return ring_.at(a).gain_db; },
                                           false, false);
        g.setColour(colour::kAccent);
        g.strokePath(gain, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (hover_x_ && columns > 0) {
            drawReadout(g, *hover_x_);
        }
    }

    void HistoryView::drawReadout(juce::Graphics& g, const float x) const {
        const auto age = static_cast<size_t>(std::max(0.f, std::round(time_.xToAge(x))));
        if (age >= ring_.size() || age >= time_.getSpan()) {
            return;
        }
        const auto& c = ring_.at(age);
        const auto px = time_.ageToX(static_cast<float>(age));
        g.setColour(colour::kHairlineStrong);
        g.fillRect(juce::Rectangle<float>(px, plot_.getY(), 1.f, plot_.getHeight()));

        const auto seconds = static_cast<double>(age) * p_ref_.getEngine().getHopSamples() / std::max(p_ref_.getSampleRate(), 1.0);
        const auto level = [](const float db) { return db <= dsp::kSilenceDb ? juce::String("--") : params::formatDb(db); };
        const std::array<std::pair<juce::String, juce::String>, 4> rows{
            {
                {"", ui::format::kMinus + juce::String(seconds, 2) + " s"}, {"In", level(c.in_db)},
                {"Out", level(c.out_db)}, {"Gain", params::formatDb(c.gain_db)}
            }
        };
        auto box = juce::Rectangle<float>(0.f, level_area_.getY() + size::kS2, 118.f, 16.f * rows.size() + size::kS2);
        box.setX(px + 10.f + box.getWidth() > plot_.getRight() ? px - 10.f - box.getWidth() : px + 10.f);
        g.setColour(colour::kOverlay);
        g.fillRoundedRectangle(box, size::kRadiusSmall);
        auto inner = box.reduced(size::kS2, size::kS1);
        for (const auto& [name, value] : rows) {
            auto row = inner.removeFromTop(16.f);
            g.setColour(colour::kTextTertiary);
            g.setFont(Fonts::get().font(type::kSmall));
            g.drawText(name, row, juce::Justification::centredLeft);
            g.setColour(colour::kTextPrimary);
            Fonts::get().drawTabular(g, value, row, juce::Justification::centredRight, type::kSmall);
        }
    }

    HistoryView::Line HistoryView::lineAt(const juce::Point<float> position) const {
        if (!level_area_.contains(position)) {
            return Line::kNone;
        }
        const auto target = std::abs(position.y - level_.dbToY(targetDb()));
        const auto floor = std::abs(position.y - level_.dbToY(floorDb()));
        if (std::min(target, floor) > size::kLineHitDistance) {
            return Line::kNone;
        }
        return target <= floor ? Line::kTarget : Line::kFloor;
    }

    juce::RangedAudioParameter* HistoryView::parameterFor(const Line line) const {
        switch (line) {
            case Line::kTarget: return p_ref_.parameters_.getParameter(params::kTarget);
            case Line::kFloor: return p_ref_.parameters_.getParameter(params::kSensitivity);
            case Line::kNone: return nullptr;
        }
        return nullptr;
    }

    int HistoryView::getHoveredParameterIndex() const {
        const auto* para = parameterFor(drag_line_ != Line::kNone ? drag_line_ : hover_line_);
        return para == nullptr ? -1 : para->getParameterIndex();
    }

    void HistoryView::mouseMove(const juce::MouseEvent& e) {
        hover_line_ = lineAt(e.position);
        hover_x_ = hover_line_ == Line::kNone && plot_.contains(e.position) ? std::optional<float>(e.position.x) : std::nullopt;
        setMouseCursor(hover_line_ == Line::kNone ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
        repaint();
    }

    void HistoryView::mouseExit(const juce::MouseEvent&) {
        hover_line_ = Line::kNone;
        hover_x_.reset();
        repaint();
    }

    void HistoryView::mouseDown(const juce::MouseEvent& e) {
        drag_line_ = lineAt(e.position);
        if (auto* para = parameterFor(drag_line_)) {
            juce::RangedAudioParameter* paras[] = {para};
            editor_.beginEdit(paras, drag_line_ == Line::kTarget ? "Change Target" : "Change Sensitivity");
            hover_x_.reset();
        }
    }

    void HistoryView::setLineFromY(const Line line, const float y) {
        const auto db = level_.yToDB(y);
        if (line == Line::kTarget) {
            editor_.setPlain(parameterFor(line), db);
        } else if (line == Line::kFloor) {
            // the floor sits 24 dB below the target, moved by the sensitivity
            editor_.setPlain(parameterFor(line), targetDb() - 24.f - db);
        }
        repaint();
    }

    void HistoryView::mouseDrag(const juce::MouseEvent& e) {
        if (drag_line_ != Line::kNone) {
            setLineFromY(drag_line_, e.position.y);
        }
    }

    void HistoryView::mouseUp(const juce::MouseEvent&) {
        if (drag_line_ != Line::kNone) {
            editor_.endEdit();
            drag_line_ = Line::kNone;
            repaint();
        }
    }

    void HistoryView::mouseDoubleClick(const juce::MouseEvent& e) {
        if (auto* para = parameterFor(lineAt(e.position))) {
            editor_.setNow({{para, para->convertFrom0to1(para->getDefaultValue())}}, "Reset " + para->getName(32));
        }
    }
}
