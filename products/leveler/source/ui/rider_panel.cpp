// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "rider_panel.hpp"

#include "theme/leveler_tokens.hpp"

namespace altvane::leveler {
    using namespace tokens;
    using ui::Fonts;

    // ---------------------------------------------------------------- RiderFader

    RiderFader::RiderFader(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        setLabel("Rider");
        setTitle("Rider fader");
        setComponentID(params::kRide);
        setRange({-scale_, scale_, .01f}, 0.f);
        format = [](const float v) { return params::formatDb(v); };
        parse = [](const juce::String& t) -> std::optional<float> {
            const auto clean = t.replace(juce::String::fromUTF8("\xe2\x88\x92"), "-").retainCharacters("+-.0123456789");
            return clean.containsAnyOf("0123456789") ? std::optional<float>(clean.getFloatValue()) : std::nullopt;
        };
        // a hand on the fader: in Read mode it edits the automation, otherwise it rides over the engine
        on_begin = [this] {
            riding_ = true;
            if (p_ref_.getMode() == dsp::Mode::kRead) {
                p_ref_.getRide()->beginChangeGesture();
                read_gesture_ = true;
            } else {
                p_ref_.beginManualRide();
            }
        };
        on_change = [this](const float v) {
            if (read_gesture_) {
                p_ref_.getRide()->setValueNotifyingHost(p_ref_.getRide()->convertTo0to1(v));
            } else {
                p_ref_.setManualRide(v);
            }
        };
        on_end = [this] {
            riding_ = false;
            if (read_gesture_) {
                p_ref_.getRide()->endChangeGesture();
                read_gesture_ = false;
            } else {
                p_ref_.endManualRide();
            }
        };
        setAccent(colour::kAccent);
    }

    RiderFader::~RiderFader() {
        if (riding_ && on_end) {
            on_end();
        }
    }

    float RiderFader::param(const char* id) const {
        return p_ref_.parameters_.getRawParameterValue(id)->load();
    }

    float RiderFader::dbToY(const float db) const {
        const auto u = (std::clamp(db, -scale_, scale_) + scale_) / (2.f * scale_);
        return track_.getBottom() - u * track_.getHeight();
    }

    float RiderFader::yToDB(const float y) const {
        return (track_.getBottom() - y) / track_.getHeight() * 2.f * scale_ - scale_;
    }

    void RiderFader::resized() {
        auto b = getLocalBounds().toFloat();
        readout_ = b.removeFromBottom(20.f);
        b.removeFromBottom(size::kS2);
        track_ = b.reduced(0.f, 10.f).withSizeKeepingCentre(6.f, b.getHeight() - 20.f);
        drag_pixels_ = std::max(track_.getHeight(), 60.f);
    }

    void RiderFader::tick() {
        const auto extent = std::max(std::abs(param(params::kRangeMin)), std::abs(param(params::kRangeMax)));
        const auto scale = extent > 12.f ? 24.f : 12.f;
        if (std::abs(scale - scale_) > .5f) {
            scale_ = scale;
            setRange({-scale_, scale_, .01f}, 0.f);
        }
        if (!dragging_) {
            setValue(p_ref_.getEngine().getGainDb());
        }
        // the handles and the cap's write outline move without the value changing
        const std::array<float, 4> look{
            param(params::kRangeMin), param(params::kRangeMax), param(params::kIdle),
            p_ref_.isWritingAutomation() ? 1.f : 0.f
        };
        if (look != look_) {
            look_ = look;
            repaint();
        }
    }

    RiderFader::Handle RiderFader::handleAt(const juce::Point<float> position) const {
        auto near = [&](const float db, const bool left) {
            const auto y = dbToY(db);
            const auto zone = left ? juce::Rectangle<float>(track_.getX() - 22.f, y - 7.f, 20.f, 14.f)
                                   : juce::Rectangle<float>(track_.getRight() + 2.f, y - 7.f, 20.f, 14.f);
            return zone.contains(position);
        };
        if (near(param(params::kRangeMax), true)) {
            return Handle::kMax;
        }
        if (near(param(params::kRangeMin), true)) {
            return Handle::kMin;
        }
        if (near(param(params::kIdle), false)) {
            return Handle::kIdle;
        }
        return Handle::kNone;
    }

    juce::RangedAudioParameter* RiderFader::parameterFor(const Handle handle) const {
        switch (handle) {
            case Handle::kMin: return p_ref_.parameters_.getParameter(params::kRangeMin);
            case Handle::kMax: return p_ref_.parameters_.getParameter(params::kRangeMax);
            case Handle::kIdle: return p_ref_.parameters_.getParameter(params::kIdle);
            case Handle::kNone: return nullptr;
        }
        return nullptr;
    }

    juce::AudioProcessorParameter* RiderFader::getHoveredParameter() const {
        if (auto* para = parameterFor(drag_ != Handle::kNone ? drag_ : hover_)) {
            return para;
        }
        return p_ref_.getRide();
    }

    void RiderFader::mouseMove(const juce::MouseEvent& e) {
        const auto handle = handleAt(e.position);
        if (handle != hover_) {
            hover_ = handle;
            setMouseCursor(handle == Handle::kNone ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
            repaint();
        }
    }

    void RiderFader::mouseExit(const juce::MouseEvent& e) {
        hover_ = Handle::kNone;
        ValueControl::mouseExit(e);
    }

    void RiderFader::mouseDown(const juce::MouseEvent& e) {
        const auto handle = e.mods.isPopupMenu() ? Handle::kNone : handleAt(e.position);
        if (handle == Handle::kNone) {
            ValueControl::mouseDown(e);
            return;
        }
        drag_ = handle;
        juce::RangedAudioParameter* paras[] = {parameterFor(handle)};
        editor_.beginEdit(paras, "Change " + paras[0]->getName(32));
    }

    void RiderFader::mouseDrag(const juce::MouseEvent& e) {
        if (drag_ == Handle::kNone) {
            ValueControl::mouseDrag(e);
            return;
        }
        editor_.setPlain(parameterFor(drag_), yToDB(e.position.y));
        repaint();
    }

    void RiderFader::mouseUp(const juce::MouseEvent& e) {
        if (drag_ == Handle::kNone) {
            ValueControl::mouseUp(e);
            return;
        }
        editor_.endEdit();
        drag_ = Handle::kNone;
        repaint();
    }

    void RiderFader::paint(juce::Graphics& g) {
        const auto cx = track_.getCentreX();
        // scale marks every 6 dB on the right
        for (auto db = -scale_; db <= scale_ + .01f; db += 6.f) {
            const auto y = std::round(dbToY(db));
            g.setColour(std::abs(db) < .01f ? colour::kGridZero : colour::kHairlineStrong);
            g.fillRect(juce::Rectangle<float>(track_.getRight() + 24.f, y, 6.f, 1.f));
            const auto text = std::abs(db) < .01f ? juce::String("0")
                                                  : (db < 0.f ? juce::String::fromUTF8("\xe2\x88\x92") : juce::String("+")) + juce::String(juce::roundToInt(std::abs(db)));
            g.setColour(colour::kTextTertiary);
            Fonts::get().drawTabular(g, text, juce::Rectangle<float>(track_.getRight() + 32.f, y - 7.f, 24.f, 14.f),
                                     juce::Justification::centredLeft, type::kCaption);
        }

        // track, with the range lit
        g.setColour(colour::kControl);
        g.fillRoundedRectangle(track_, 3.f);
        const auto lo = dbToY(param(params::kRangeMin));
        const auto hi = dbToY(param(params::kRangeMax));
        g.setColour(colour::kAccent.withAlpha(.3f));
        g.fillRoundedRectangle(juce::Rectangle<float>(track_.getX(), hi, track_.getWidth(), lo - hi), 3.f);

        // range handles point at the track from the left, the idle marker from the right
        auto triangle = [&](const float y, const bool from_left, const bool lit) {
            juce::Path p;
            const auto x = from_left ? track_.getX() - 4.f : track_.getRight() + 4.f;
            const auto d = from_left ? -9.f : 9.f;
            p.addTriangle(x, y, x + d, y - 5.f, x + d, y + 5.f);
            g.setColour(lit ? colour::kTextPrimary : colour::kTextSecondary);
            g.fillPath(p);
        };
        const auto active = [&](const Handle h) { return hover_ == h || drag_ == h; };
        triangle(hi, true, active(Handle::kMax));
        triangle(lo, true, active(Handle::kMin));
        triangle(dbToY(std::clamp(param(params::kIdle), param(params::kRangeMin), param(params::kRangeMax))), false,
                 active(Handle::kIdle));

        // the cap
        const auto writing = p_ref_.isWritingAutomation();
        const auto cap = juce::Rectangle<float>(36.f, 16.f).withCentre({cx, dbToY(getValue())});
        g.setColour(dragging_ ? colour::kControlHover : colour::kRaised);
        g.fillRoundedRectangle(cap, size::kRadiusSmall);
        g.setColour(writing ? colour::kWrite : dragging_ || hovered_ ? colour::kTextSecondary : colour::kHairlineStrong);
        g.drawRoundedRectangle(cap.reduced(.5f), size::kRadiusSmall, 1.f);
        g.setColour(dragging_ ? colour::kTextPrimary : colour::kAccent);
        g.fillRect(juce::Rectangle<float>(cap.getWidth() - 12.f, 2.f).withCentre(cap.getCentre()));

        g.setColour(colour::kTextPrimary);
        Fonts::get().drawTabular(g, params::formatDb(getValue()), readout_, juce::Justification::centred, type::kValue);
        if (focus_ring_) {
            paintFocusRing(g, cap.expanded(3.f), size::kRadiusSmall + 2.f);
        }
    }

    // ---------------------------------------------------------------- RiderPanel

    RiderPanel::RiderPanel(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), fader_(p, editor) {
        setTitle("Rider");
        setFocusContainerType(FocusContainerType::focusContainer);
        const std::array<juce::String, 3> tips{
            "Off: ride, and tell the host nothing",
            "Write: ride, and write the rider fader to the host's automation while the transport plays "
            "(arm the Rider parameter in the host)",
            "Read: follow the rider fader's automation instead of riding"
        };
        for (size_t m = 0; m < chips_.size(); ++m) {
            auto& chip = *chips_[m];
            chip.setTooltip(tips[m]);
            chip.setOnColour(m == static_cast<size_t>(dsp::Mode::kWrite) ? colour::kWrite : colour::kAccent);
            chip.on_click = [this, m] { p_ref_.setMode(static_cast<dsp::Mode>(m)); };
            addAndMakeVisible(chip);
        }
        addAndMakeVisible(fader_);
        ui::bindValue(max_, editor, p.parameters_.getParameter(params::kRangeMax), "Max");
        ui::bindValue(min_, editor, p.parameters_.getParameter(params::kRangeMin), "Min");
        ui::bindValue(idle_, editor, p.parameters_.getParameter(params::kIdle), "Idle");
        for (auto* field : {&max_, &min_, &idle_}) {
            field->format = [](const float v) { return params::formatDb(v); };
            addAndMakeVisible(field);
        }
        max_.setDescription("Range Max: the most the rider boosts");
        min_.setDescription("Range Min: the most the rider cuts");
        idle_.setDescription("Idle: where the fader settles after a longer silence");
        tick();
    }

    void RiderPanel::tick() {
        const auto mode = p_ref_.getMode();
        for (size_t m = 0; m < chips_.size(); ++m) {
            chips_[m]->setOn(static_cast<size_t>(mode) == m);
        }
        const auto& t = p_ref_.parameters_;
        max_.setValue(t.getRawParameterValue(params::kRangeMax)->load());
        min_.setValue(t.getRawParameterValue(params::kRangeMin)->load());
        idle_.setValue(t.getRawParameterValue(params::kIdle)->load());
        fader_.tick();
        const auto state = p_ref_.getEngine().getState();
        const auto writing = p_ref_.isWritingAutomation();
        if (state != state_ || writing != writing_) {
            state_ = state;
            writing_ = writing;
            repaint(state_area_.toNearestInt());
        }
    }

    void RiderPanel::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, size::kS3);
        auto top = b.removeFromTop(16.f);
        state_area_ = top.removeFromRight(64.f);
        title_area_ = top;
        b.removeFromTop(size::kS2);
        auto chips = b.removeFromTop(size::kControlHeight);
        const auto w = (chips.getWidth() - 4.f) / 3.f;
        for (auto* chip : chips_) {
            chip->setBounds(chips.removeFromLeft(w).toNearestInt());
            chips.removeFromLeft(2.f);
        }
        b.removeFromTop(size::kS2);
        for (auto* field : {&idle_, &min_, &max_}) {
            field->setBounds(b.removeFromBottom(20.f).toNearestInt());
        }
        b.removeFromBottom(size::kS2);
        fader_.setBounds(b.toNearestInt());
    }

    void RiderPanel::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, 1.f, b.getHeight()));
        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("RIDER", title_area_, juce::Justification::centredLeft);

        // what the rider is doing: riding a voice, guarding against breaths and sibilants, or idle
        juce::String text;
        juce::Colour c;
        if (writing_) {
            text = "WRITE";
            c = colour::kWrite;
        } else {
            switch (state_) {
                case dsp::HopState::kVoiced: text = "RIDE";
                    c = colour::kVoiced;
                    break;
                case dsp::HopState::kUnvoiced: text = "GUARD";
                    c = colour::kUnvoiced;
                    break;
                case dsp::HopState::kSilent: text = "IDLE";
                    c = colour::kTextTertiary;
                    break;
            }
        }
        auto area = state_area_;
        g.setColour(c);
        g.fillEllipse(area.removeFromRight(6.f).withSizeKeepingCentre(6.f, 6.f));
        area.removeFromRight(size::kS1);
        g.drawText(text, area, juce::Justification::centredRight);
    }
}
