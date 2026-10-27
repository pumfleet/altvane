// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "knee_display.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "graph_helpers.hpp"
#include "../model/formats.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;
    using Direction = zlp::PCompDirection::Direction;

    namespace {
        constexpr size_t kTrailLength = 14;
        // the dot falls back at this rate (dB per second) when the level drops
        constexpr float kDotFall = 30.f;

        bool isBelowThresholdDirection(const Direction d) {
            return d == zlp::PCompDirection::kExpand || d == zlp::PCompDirection::kInflate;
        }
    }

    KneeDisplay::KneeDisplay(CompBridge& bridge, TransferModel& model) : bridge_(bridge), model_(model) {
        setTitle("Knee display");
        setDescription("The transfer curve. Drag the elbow for threshold, the slope for ratio, "
            "the shoulders for knee width.");
    }

    KneeDisplay::~KneeDisplay() {
        timerCallback();
    }

    juce::Rectangle<float> KneeDisplay::getPlot() const {
        auto b = getLocalBounds().toFloat();
        b.removeFromLeft(kPad);
        b.removeFromBottom(kPad);
        b.removeFromTop(kEdge);
        b.removeFromRight(kEdge);
        return b;
    }

    float KneeDisplay::xOf(const float db) const {
        const auto p = getPlot();
        return p.getX() + (1.f + db / scale_db_) * p.getWidth();
    }

    float KneeDisplay::yOf(const float db) const {
        const auto p = getPlot();
        return p.getY() + (-db / scale_db_) * p.getHeight();
    }

    float KneeDisplay::dbOfX(const float x) const {
        const auto p = getPlot();
        return ((x - p.getX()) / std::max(p.getWidth(), 1.f) - 1.f) * scale_db_;
    }

    float KneeDisplay::dbOfY(const float y) const {
        const auto p = getPlot();
        return -((y - p.getY()) / std::max(p.getHeight(), 1.f)) * scale_db_;
    }

    void KneeDisplay::tick(const double dt, const std::optional<float> detector_db) {
        scale_db_ = bridge_.getScaleDB();
        model_.sync(bridge_);
        const auto target = detector_db.value_or(-120.f);
        dot_db_ = target > dot_db_ ? target : std::max(target, dot_db_ - kDotFall * static_cast<float>(dt));
        trail_.push_back(dot_db_);
        while (trail_.size() > kTrailLength) {
            trail_.pop_front();
        }
        repaint();
    }

    juce::Point<float> KneeDisplay::handlePosition(const Handle h) {
        const auto& s = model_.getSettings();
        const auto t = s.threshold;
        float x = t;
        switch (h) {
        case Handle::kElbow:
        case Handle::kNone:
            break;
        case Handle::kKneeLow:
            x = t - s.knee;
            break;
        case Handle::kKneeHigh:
            x = t + s.knee;
            break;
        case Handle::kRatio:
            x = isBelowThresholdDirection(s.direction)
                    ? std::max(t - std::max(6.f, (t + scale_db_) * .55f), -scale_db_ + 1.f)
                    : std::min(-.5f, t + std::max(6.f, -t * .62f));
            break;
        }
        return {x, model_.outputFor(x)};
    }

    juce::String KneeDisplay::parameterFor(const Handle h) {
        switch (h) {
        case Handle::kElbow:
            return zlp::PThreshold::kID;
        case Handle::kRatio:
            return zlp::PRatio::kID;
        case Handle::kKneeLow:
        case Handle::kKneeHigh:
            return zlp::PKneeW::kID;
        case Handle::kNone:
            break;
        }
        return {};
    }

    KneeDisplay::Handle KneeDisplay::handleAt(const juce::Point<float> p) {
        auto best = Handle::kNone;
        auto best_d = 12.f;
        for (const auto h : {Handle::kElbow, Handle::kRatio, Handle::kKneeLow, Handle::kKneeHigh}) {
            const auto pos = handlePosition(h);
            if (pos.x < -scale_db_ || pos.x > 0.f) {
                continue;
            }
            const auto d = p.getDistanceFrom({xOf(pos.x), yOf(pos.y)});
            if (d < best_d) {
                best_d = d;
                best = h;
            }
        }
        return best;
    }

    void KneeDisplay::paint(juce::Graphics& g) {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(colour::kGraph.withAlpha(.94f));
        g.fillRoundedRectangle(bounds, size::kRadiusLarge);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(bounds.reduced(.5f), size::kRadiusLarge, 1.f);

        const auto plot = getPlot();
        const auto step = scale_db_ <= 30.f ? 6.f : 12.f;
        g.setFont(graph::labelFont());
        for (float d = 0.f; d <= scale_db_ + .01f; d += step) {
            const auto x = std::round(xOf(-d)) + .5f;
            const auto y = std::round(yOf(-d)) + .5f;
            g.setColour(d == 0.f ? juce::Colour{0x1FFFFFFF} : juce::Colour{0x0DFFFFFF});
            g.fillRect(juce::Rectangle<float>(x - .5f, plot.getY(), 1.f, plot.getHeight()));
            g.fillRect(juce::Rectangle<float>(plot.getX(), y - .5f, plot.getWidth(), 1.f));
            if (d > 0.f && std::fmod(d, step * 2.f) < .01f) {
                g.setColour(juce::Colour{0x4DFFFFFF});
                g.drawText(graph::gridDB(-d), juce::Rectangle<float>(x - 16.f, plot.getBottom() + 5.f, 32.f, 12.f),
                           juce::Justification::centredTop);
                g.drawText(graph::gridDB(-d), juce::Rectangle<float>(0.f, y - 6.f, kPad - 6.f, 12.f),
                           juce::Justification::centredRight);
            }
        }
        g.setFont(Fonts::get().font(9.f, Fonts::kSemiBold));
        g.setColour(juce::Colour{0x61FFFFFF});
        g.drawText("IN", juce::Rectangle<float>(plot.getRight() - 30.f, plot.getBottom() + 5.f, 30.f, 12.f),
                   juce::Justification::topRight);
        g.drawText("OUT", juce::Rectangle<float>(8.f, 6.f, 40.f, 12.f), juce::Justification::topLeft);

        const auto& s = model_.getSettings();
        const auto bypassed = !bridge_.getBool(zlp::PCompON::kID);
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(plot.toNearestInt());

            // 1:1 reference
            constexpr float dashes[] = {3.f, 4.f};
            g.setColour(juce::Colour{0x24FFFFFF});
            g.drawDashedLine({xOf(-scale_db_), yOf(-scale_db_), xOf(0.f), yOf(0.f)}, dashes, 2, 1.f);

            // the knee, and the threshold
            const auto t = s.threshold;
            g.setColour(colour::kAccent.withAlpha(.05f));
            g.fillRect(juce::Rectangle<float>(xOf(t - s.knee), plot.getY(), xOf(t + s.knee) - xOf(t - s.knee),
                                              plot.getHeight()));
            g.setColour(colour::kAccent.withAlpha(.25f));
            g.fillRect(juce::Rectangle<float>(std::round(xOf(t)), plot.getY(), 1.f, plot.getHeight()));

            // the curve, lit up to the detector level
            juce::Path curve, lit;
            auto lit_started = false;
            for (auto px = plot.getX(); px <= plot.getRight() + .5f; px += 1.f) {
                const auto in = dbOfX(px);
                const auto y = yOf(model_.outputFor(in));
                if (px <= plot.getX()) {
                    curve.startNewSubPath(px, y);
                } else {
                    curve.lineTo(px, y);
                }
                if (!bypassed && in <= dot_db_) {
                    if (!lit_started) {
                        lit.startNewSubPath(px, y);
                        lit_started = true;
                    } else {
                        lit.lineTo(px, y);
                    }
                }
            }
            const juce::PathStrokeType stroke(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            g.setColour(colour::kCurve.withAlpha(.9f));
            g.strokePath(curve, stroke);
            if (!lit.isEmpty()) {
                g.setColour(colour::kAccent.withAlpha(.18f));
                g.strokePath(lit, juce::PathStrokeType(7.f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
                g.setColour(colour::kAccent);
                g.strokePath(lit, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }

            // trail and dot
            if (!bypassed && dot_db_ > -scale_db_) {
                for (size_t i = 0; i < trail_.size(); ++i) {
                    const auto a = static_cast<float>(i + 1) / static_cast<float>(trail_.size());
                    const auto d = trail_[i];
                    g.setColour(colour::kAccent.withAlpha(a * .25f));
                    const auto r = 1.5f + a * 2.f;
                    g.fillEllipse(juce::Rectangle<float>(r * 2.f, r * 2.f)
                        .withCentre({xOf(d), yOf(model_.outputFor(d))}));
                }
                const juce::Point<float> dot{xOf(dot_db_), yOf(model_.outputFor(dot_db_))};
                g.setColour(colour::kAccent.withAlpha(.25f));
                g.fillEllipse(juce::Rectangle<float>(18.f, 18.f).withCentre(dot));
                g.setColour(juce::Colours::white);
                g.fillEllipse(juce::Rectangle<float>(8.f, 8.f).withCentre(dot));
            }
        }

        // handles
        const auto active = drag_ != Handle::kNone ? drag_ : hover_;
        const auto drawHandle = [&](const Handle h, const float r) {
            const auto pos = handlePosition(h);
            if (pos.x < -scale_db_ || pos.x > 0.f) {
                return;
            }
            const auto on = active == h;
            const auto rr = on ? r + 1.5f : r;
            const auto area = juce::Rectangle<float>(rr * 2.f, rr * 2.f).withCentre({xOf(pos.x), yOf(pos.y)});
            g.setColour(on ? colour::kAccent : colour::kGraph);
            g.fillEllipse(area);
            g.setColour(on ? colour::kAccent : colour::kCurve.withAlpha(.8f));
            g.drawEllipse(area, 1.5f);
        };
        if (active == Handle::kKneeLow || active == Handle::kKneeHigh || active == Handle::kElbow) {
            drawHandle(Handle::kKneeLow, 3.f);
            drawHandle(Handle::kKneeHigh, 3.f);
        }
        drawHandle(Handle::kRatio, 4.f);
        drawHandle(Handle::kElbow, 5.f);

        // readout
        juce::String info;
        switch (active) {
        case Handle::kElbow:
            info = "Threshold " + formats::db(s.threshold);
            break;
        case Handle::kRatio:
            info = "Ratio " + formats::ratio(s.ratio);
            break;
        case Handle::kKneeLow:
        case Handle::kKneeHigh:
            info = "Knee " + formats::db(s.knee);
            break;
        case Handle::kNone:
            if (hover_input_.has_value()) {
                info = "In " + formats::db(*hover_input_) + juce::String::fromUTF8("  \xe2\x86\x92  ") + "Out "
                       + formats::db(model_.outputFor(*hover_input_));
            }
            break;
        }
        if (info.isNotEmpty()) {
            g.setColour(colour::kTextPrimary.withAlpha(.85f));
            Fonts::get().drawTabular(g, info, juce::Rectangle<float>(plot.getX(), 6.f, plot.getWidth() - 2.f, 14.f),
                                     juce::Justification::centredRight, type::kSmall);
        }
    }

    void KneeDisplay::mouseMove(const juce::MouseEvent& e) {
        hover_ = handleAt(e.position);
        const auto plot = getPlot();
        hover_input_ = hover_ == Handle::kNone && plot.contains(e.position)
                           ? std::optional(dbOfX(e.position.x))
                           : std::nullopt;
        switch (hover_) {
        case Handle::kElbow:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case Handle::kRatio:
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            break;
        case Handle::kKneeLow:
        case Handle::kKneeHigh:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case Handle::kNone:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
        }
        repaint();
    }

    void KneeDisplay::mouseExit(const juce::MouseEvent&) {
        if (drag_ == Handle::kNone) {
            hover_ = Handle::kNone;
            hover_input_.reset();
            repaint();
        }
    }

    void KneeDisplay::mouseDown(const juce::MouseEvent& e) {
        timerCallback();
        const auto h = handleAt(e.position);
        if (h == Handle::kNone || e.mods.isPopupMenu()) {
            return;
        }
        const auto id = parameterFor(h);
        auto* para = bridge_.para(id);
        if (e.mods.isCommandDown()) {
            bridge_.set(id, para->convertFrom0to1(para->getDefaultValue()), para->getName(64));
            return;
        }
        const auto& s = model_.getSettings();
        const auto makeup = s.makeup * s.wet * .01f;
        drag_offset_ = h == Handle::kElbow
                           ? s.threshold - (std::clamp(dbOfX(e.position.x), -100.f, 0.f) + dbOfY(e.position.y)
                                            - makeup) * .5f
                           : 0.f;
        last_drag_y_ = e.position.y;
        drag_ = h;
        juce::RangedAudioParameter* paras[] = {para};
        bridge_.getEditor().beginEdit(paras, "Change " + para->getName(64));
    }

    void KneeDisplay::mouseDrag(const juce::MouseEvent& e) {
        if (drag_ == Handle::kNone) {
            return;
        }
        const auto& s = model_.getSettings();
        const auto makeup = s.makeup * s.wet * .01f;
        const auto x = std::clamp(dbOfX(e.position.x), -100.f, 0.f);
        const auto y = dbOfY(e.position.y) - makeup;
        auto& editor = bridge_.getEditor();
        switch (drag_) {
        case Handle::kElbow: {
            // along the 1:1 line: both axes count, so diagonal drags feel natural
            const auto t = std::clamp((x + y) * .5f + drag_offset_, -100.f, 0.f);
            editor.setPlain(bridge_.para(zlp::PThreshold::kID), std::round(t * 10.f) / 10.f);
            break;
        }
        case Handle::kRatio: {
            const auto t = s.threshold;
            auto ratio = s.ratio;
            switch (s.direction) {
            case zlp::PCompDirection::kCompress:
                ratio = std::max(.5f, x - t) / std::max(.01f, y - t);
                break;
            case zlp::PCompDirection::kShape:
                ratio = std::max(.01f, y - t) / std::max(.5f, x - t);
                break;
            case zlp::PCompDirection::kExpand:
                ratio = std::min(-.01f, y - t) / std::min(-.5f, x - t);
                break;
            case zlp::PCompDirection::kInflate:
                ratio = s.ratio * (1.f + (last_drag_y_ - e.position.y) * .01f);
                break;
            }
            editor.setPlain(bridge_.para(zlp::PRatio::kID), std::clamp(std::round(ratio * 100.f) / 100.f, 1.f, 100.f));
            break;
        }
        case Handle::kKneeLow:
        case Handle::kKneeHigh:
            editor.setPlain(bridge_.para(zlp::PKneeW::kID),
                            std::clamp(std::round(std::abs(x - s.threshold) * 10.f) / 10.f, 0.f, 32.f));
            break;
        case Handle::kNone:
            break;
        }
        last_drag_y_ = e.position.y;
        model_.sync(bridge_);
        repaint();
    }

    void KneeDisplay::mouseUp(const juce::MouseEvent& e) {
        if (drag_ != Handle::kNone) {
            drag_ = Handle::kNone;
            bridge_.getEditor().endEdit();
            mouseMove(e);
        }
    }

    void KneeDisplay::mouseDoubleClick(const juce::MouseEvent& e) {
        const auto h = handleAt(e.position);
        if (h == Handle::kNone) {
            return;
        }
        const auto id = parameterFor(h);
        auto* para = bridge_.para(id);
        bridge_.set(id, para->convertFrom0to1(para->getDefaultValue()), para->getName(64));
    }

    void KneeDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
        if (drag_ != Handle::kNone) {
            return;
        }
        const auto id = juce::String(e.mods.isCommandDown() ? zlp::PRatio::kID : zlp::PThreshold::kID);
        auto* para = bridge_.para(id);
        const auto delta = std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs(delta) < 1e-6f) {
            return;
        }
        if (wheel_id_ != id) {
            timerCallback();
            wheel_id_ = id;
            juce::RangedAudioParameter* paras[] = {para};
            bridge_.getEditor().beginEdit(paras, "Change " + para->getName(64));
        }
        const auto fine = e.mods.isShiftDown() ? .2f : 1.f;
        const auto norm = std::clamp(para->getValue() + delta * (wheel.isReversed ? -1.f : 1.f) * .05f * fine,
                                     0.f, 1.f);
        bridge_.getEditor().setPlain(para, para->convertFrom0to1(norm));
        startTimer(450);
    }

    void KneeDisplay::timerCallback() {
        stopTimer();
        if (wheel_id_.isNotEmpty()) {
            wheel_id_.clear();
            bridge_.getEditor().endEdit();
        }
    }
}
