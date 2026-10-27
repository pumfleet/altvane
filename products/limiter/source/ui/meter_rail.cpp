// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "meter_rail.hpp"

namespace altvane::limiter {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kOutTop = 0.f, kOutBottom = -48.f;
        constexpr float kReductionRange = 24.f;
        constexpr float kFallPerSecond = 24.f;

        float fall(const float current, const float target, const double dt) {
            return target >= current ? target : std::max(target, current - kFallPerSecond * static_cast<float>(dt));
        }

        float rise(const float current, const float target, const double dt) {
            return target <= current ? target : std::min(target, current + kFallPerSecond * static_cast<float>(dt));
        }

        void bar(juce::Graphics& g, const juce::Rectangle<float> track, const float fraction, const juce::Colour top,
                 const juce::Colour bottom_colour, const bool from_top = false) {
            g.setColour(colour::kControl);
            g.fillRoundedRectangle(track, 1.5f);
            const auto f = std::clamp(fraction, 0.f, 1.f);
            if (f <= 0.f) {
                return;
            }
            const auto fill = from_top ? track.withHeight(track.getHeight() * f)
                                       : track.withTop(track.getBottom() - track.getHeight() * f);
            g.setGradientFill(juce::ColourGradient(top, 0.f, track.getY(), bottom_colour, 0.f, track.getBottom(), false));
            g.fillRoundedRectangle(fill, 1.5f);
        }

        void readout(juce::Graphics& g, const juce::Rectangle<float> box, const juce::String& caption,
                     const juce::String& text, const juce::Colour colour) {
            g.setColour(colour::kRaised);
            g.fillRoundedRectangle(box, size::kRadiusSmall);
            g.setColour(colour::kHairline);
            g.drawRoundedRectangle(box.reduced(.5f), size::kRadiusSmall, 1.f);
            auto b = box.reduced(4.f, 3.f);
            g.setFont(captionFont());
            g.setColour(colour::kTextTertiary);
            g.drawText(caption, b.removeFromTop(11.f), juce::Justification::centred);
            g.setColour(colour);
            Fonts::get().drawTabular(g, text, b, juce::Justification::centred, type::kBody, Fonts::kSemiBold);
        }
    }

    MeterRail::MeterRail(PluginProcessor& p) : p_ref_(p) {
        setTitle("Meters");
        setDescription("Output peak, gain reduction and loudness. Click to reset the held values.");
    }

    void MeterRail::resized() {
        auto b = getLocalBounds().toFloat().reduced(10.f, 10.f);
        b.removeFromLeft(1.f);
        readouts_ = b.removeFromTop(36.f);
        b.removeFromTop(12.f);
        labels_ = b.removeFromBottom(14.f);
        b.removeFromBottom(6.f);
        // OUT L/R | scale | GR | LUFS M/S | scale
        const auto w = b.getWidth();
        out_area_ = b.removeFromLeft(w * .19f);
        out_scale_ = b.removeFromLeft(w * .15f);
        gr_area_ = b.removeFromLeft(w * .1f);
        gr_scale_ = b.removeFromLeft(w * .12f);
        b.removeFromLeft(w * .02f);
        lufs_area_ = b.removeFromLeft(w * .2f);
        lufs_scale_ = b;
    }

    float MeterRail::loudnessReference() const {
        switch (scale_) {
        case 1: return target_.value_or(-23.f);
        case 2: return -12.f;
        case 3: return -14.f;
        case 4: return -20.f;
        default: return 0.f;
        }
    }

    std::pair<float, float> MeterRail::loudnessRange() const {
        if (scale_ == 0) {
            return {0.f, -48.f};
        }
        // relative scales: EBU's +9 scale for LU, K-system headroom for K-n (0 LUFS on top)
        const auto top = scale_ == 1 ? loudnessReference() + 9.f : 0.f;
        return {top, top - 36.f};
    }

    void MeterRail::tick(const double dt, const LevelFeed& feed, const LoudnessReadings& loudness) {
        auto& params = p_ref_.parameters_;
        auto& na = p_ref_.na_parameters_;
        ceiling_ = value(params, zlp::POutputCeiling::kID);
        true_peak_ = value(params, zlp::PTruePeak::kID) > .5f;
        target_ = currentTarget(na).lufs;
        scale_ = index(na, zlstate::PLoudnessScale::kID);

        for (size_t c = 0; c < 2; ++c) {
            const auto peak = feed.has_new ? feed.output_peaks[c] : LevelFeed::kFloor;
            out_[c] = fall(out_[c], peak, dt);
        }
        if (feed.has_new) {
            out_hold_ = std::max({out_hold_, feed.output_peaks[0], feed.output_peaks[1]});
            reduction_hold_ = std::min(reduction_hold_, feed.reduction);
        }
        reduction_ = rise(reduction_, feed.has_new ? feed.reduction : 0.f, dt);

        momentary_.setTarget(std::max(loudness.momentary, -80.f));
        short_term_.setTarget(std::max(loudness.short_term, -80.f));
        momentary_.update(static_cast<float>(dt));
        short_term_.update(static_cast<float>(dt));
        integrated_ = loudness.integrated;
        has_integrated_ = loudness.has_integrated;
        repaint();
    }

    void MeterRail::resetHolds() {
        out_hold_ = LevelFeed::kFloor;
        reduction_hold_ = 0.f;
        repaint();
    }

    void MeterRail::mouseUp(const juce::MouseEvent& e) {
        if (e.mouseWasClicked()) {
            resetHolds();
        }
    }

    void MeterRail::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, 1.f, b.getHeight()));

        // held readouts
        {
            auto r = readouts_;
            const auto w = (r.getWidth() - 8.f) / 3.f;
            const auto over = out_hold_ > ceiling_ + .05f;
            readout(g, r.removeFromLeft(w), true_peak_ ? "TP" : "PEAK", level(out_hold_, 1),
                    over ? colour::kDanger : colour::kTextPrimary);
            r.removeFromLeft(4.f);
            readout(g, r.removeFromLeft(w), "GR", reduction_hold_ < -.05f ? level(reduction_hold_, 1) : "0.0",
                    reduction_hold_ < -.05f ? colour::kReduction : colour::kTextSecondary);
            r.removeFromLeft(4.f);
            readout(g, r, "INT", has_integrated_ ? level(integrated_, 1) : juce::String("--"),
                    has_integrated_ && target_.has_value() ? targetColour(integrated_ - *target_)
                                                           : colour::kLoudness);
        }

        // output L / R
        const auto out_y = [&](const float db) {
            return out_area_.getY() + (kOutTop - std::clamp(db, kOutBottom, kOutTop)) / (kOutTop - kOutBottom) *
                   out_area_.getHeight();
        };
        const auto bar_w = 7.f;
        for (size_t c = 0; c < 2; ++c) {
            const auto track = juce::Rectangle<float>(out_area_.getCentreX() + (c == 0 ? -bar_w - 1.5f : 1.5f),
                                                      out_area_.getY(), bar_w, out_area_.getHeight());
            const auto fraction = (out_[c] - kOutBottom) / (kOutTop - kOutBottom);
            bar(g, track, fraction, colour::kMeterTop, colour::kMeterBottom);
            if (out_[c] > ceiling_ + .05f) {
                g.setColour(colour::kDanger);
                g.fillRoundedRectangle(track.withHeight(3.f), 1.5f);
            }
        }
        // the ceiling on the output meters
        {
            const auto y = out_y(ceiling_);
            g.setColour(colour::kAccent);
            juce::Path tri;
            tri.addTriangle(out_area_.getX(), y - 3.5f, out_area_.getX(), y + 3.5f, out_area_.getX() + 5.f, y);
            g.fillPath(tri);
        }
        // shared dB scale between the output and gain reduction meters
        g.setFont(Fonts::get().font(9.f));
        for (int d = 0; d <= 48; d += 6) {
            const auto y = out_y(static_cast<float>(-d));
            g.setColour(colour::kTextTertiary.withMultipliedAlpha(.85f));
            g.drawText(d == 0 ? juce::String("0") : ui::format::kMinus + juce::String(d),
                       juce::Rectangle<float>(out_scale_.getX(), y - 6.f, out_scale_.getWidth() - 2.f, 12.f),
                       juce::Justification::centred);
        }

        // gain reduction, falling from the top on its own scale
        {
            const auto track = gr_area_.withSizeKeepingCentre(bar_w + 1.f, gr_area_.getHeight());
            bar(g, track, -reduction_ / kReductionRange, colour::kReduction, colour::kReduction.withAlpha(.6f), true);
            g.setFont(Fonts::get().font(9.f));
            g.setColour(colour::kTextTertiary.withMultipliedAlpha(.85f));
            for (const auto d : {3, 6, 9, 12, 18, 24}) {
                const auto y = track.getY() + static_cast<float>(d) / kReductionRange * track.getHeight();
                g.drawText(juce::String(d), juce::Rectangle<float>(gr_scale_.getX(), y - 6.f, gr_scale_.getWidth(), 12.f),
                           juce::Justification::centredLeft);
            }
            if (reduction_hold_ < -.05f) {
                const auto y = track.getY() + std::min(1.f, -reduction_hold_ / kReductionRange) * track.getHeight();
                g.setColour(colour::kTextPrimary.withAlpha(.8f));
                g.fillRect(juce::Rectangle<float>(track.getX() - 1.f, y - .5f, track.getWidth() + 2.f, 1.f));
            }
        }

        // loudness: momentary and short-term bars, the integrated value and the target
        const auto range = loudnessRange();
        const auto top = range.first, bottom = range.second;
        const auto lufs_y = [&](const float lufs) {
            return lufs_area_.getY() + (top - std::clamp(lufs, bottom, top)) / (top - bottom) * lufs_area_.getHeight();
        };
        const auto fraction = [&](const float lufs) {
            return (lufs - bottom) / (top - bottom);
        };
        const auto m_track = juce::Rectangle<float>(lufs_area_.getCentreX() - bar_w - 1.5f, lufs_area_.getY(), bar_w,
                                                    lufs_area_.getHeight());
        const auto s_track = m_track.translated(bar_w + 3.f, 0.f);
        const auto gold = colour::kLoudness;
        bar(g, m_track, fraction(momentary_.getValue()), gold.withAlpha(.75f), gold.withAlpha(.35f));
        bar(g, s_track, fraction(short_term_.getValue()), gold, gold.withAlpha(.5f));
        if (target_.has_value()) {
            const auto y = lufs_y(*target_);
            g.setColour(gold.withAlpha(.12f));
            g.fillRect(juce::Rectangle<float>(m_track.getX() - 3.f, lufs_y(*target_ + 1.f),
                                              s_track.getRight() - m_track.getX() + 6.f,
                                              lufs_y(*target_ - 1.f) - lufs_y(*target_ + 1.f)));
            juce::Path tri;
            tri.addTriangle(m_track.getX() - 8.f, y - 3.5f, m_track.getX() - 8.f, y + 3.5f, m_track.getX() - 3.f, y);
            g.setColour(gold);
            g.fillPath(tri);
        }
        if (has_integrated_) {
            const auto y = lufs_y(integrated_);
            g.setColour(colour::kTextPrimary);
            g.fillRoundedRectangle(juce::Rectangle<float>(m_track.getX() - 2.f, y - 1.f,
                                                          s_track.getRight() - m_track.getX() + 4.f, 2.f), 1.f);
        }
        const auto reference = loudnessReference();
        for (auto lufs = top; lufs >= bottom - .01f; lufs -= 6.f) {
            const auto y = lufs_y(lufs);
            const auto shown = std::round(lufs - reference);
            g.setColour(colour::kTextTertiary.withMultipliedAlpha(.85f));
            g.drawText(shown == 0.f ? juce::String("0")
                                    : (shown > 0.f ? "+" : ui::format::kMinus) + juce::String(std::abs(static_cast<int>(shown))),
                       juce::Rectangle<float>(lufs_scale_.getX(), y - 6.f, lufs_scale_.getWidth(), 12.f),
                       juce::Justification::centredLeft);
        }

        g.setFont(captionFont());
        g.setColour(colour::kTextTertiary);
        g.drawText("OUT", juce::Rectangle<float>(out_area_.getX(), labels_.getY(), out_area_.getWidth(), labels_.getHeight()),
                   juce::Justification::centred);
        g.drawText("GR", juce::Rectangle<float>(gr_area_.getX() - 6.f, labels_.getY(), gr_area_.getWidth() + 12.f,
                                                labels_.getHeight()), juce::Justification::centred);
        const juce::String lufs_label = scale_ == 0 ? "LUFS" : scale_ == 1 ? "LU" : zlstate::PLoudnessScale::kChoices[scale_];
        g.drawText("M  S", juce::Rectangle<float>(lufs_area_.getX(), labels_.getY(), lufs_area_.getWidth(),
                                                labels_.getHeight()), juce::Justification::centred);
        g.drawText(lufs_label, juce::Rectangle<float>(lufs_scale_.getX(), labels_.getY(), lufs_scale_.getWidth(),
                                                      labels_.getHeight()), juce::Justification::centredLeft);
    }
}
