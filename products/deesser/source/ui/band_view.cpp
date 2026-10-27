// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "band_view.hpp"

#include <complex>

#include <altvane_ui/theme/fonts.hpp>

#include "theme/deesser_tokens.hpp"

namespace altvane::deesser {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kLowHz = Analyzer::kLowHz;
        constexpr float kHighHz = Analyzer::kHighHz;
        // spectrum: tilted so a voice's treble reads beside its body (dB per octave around 1 kHz)
        constexpr float kTiltDb = 4.5f;
        constexpr float kSpectrumTopDb = 0.f;
        constexpr float kSpectrumBottomDb = -84.f;
        // the gain curve: 0 dB near the top, the deepest reduction near the bottom
        constexpr float kResponseTop = .26f;
        constexpr float kResponseBottom = .92f;
        constexpr float kResponseScaleDb = 24.f;
        constexpr double kOpenBellTop = 16000.0;

        juce::String shortHz(const float hz) {
            return hz >= 1000.f ? juce::String(juce::roundToInt(hz / 1000.f)) + "k" : juce::String(juce::roundToInt(hz));
        }
    }

    BandView::BandView(PluginProcessor& p, ui::ParamEditor& editor, Analyzer& analyzer) :
        p_ref_(p), editor_(editor), analyzer_(analyzer) {
        setTitle("Detection band");
        setDescription("The spectrum, the band the de-esser listens to, and the gain it applies");
        auto bind = [&](ui::ValueField& field, const char* id, const juce::String& name, auto&& format) {
            auto* para = p.parameters_.getParameter(id);
            ui::bindValue(field, editor_, para, name);
            field.format = format;
            field.parse = [para](const juce::String& t) -> std::optional<float> {
                // a number, or "open"; anything else leaves the value alone
                if (!t.containsAnyOf("0123456789") && !t.trim().equalsIgnoreCase("open")) {
                    return std::nullopt;
                }
                return para->convertFrom0to1(para->getValueForText(t));
            };
            addAndMakeVisible(field);
        };
        bind(low_field_, params::kBandLow, "Low", [](const float v) { return params::formatHz(v); });
        bind(high_field_, params::kBandHigh, "High", [](const float v) { return params::formatBandHigh(v); });
        // an edge set from its field pushes the other along, in the same edit, as dragging it does
        for (auto* field : {&low_field_, &high_field_}) {
            const auto is_low = field == &low_field_;
            field->on_begin = [this, is_low] {
                juce::RangedAudioParameter* paras[] = {param(params::kBandLow), param(params::kBandHigh)};
                editor_.beginEdit(paras, is_low ? "Change Band Low" : "Change Band High");
                field_edit_id_ = editor_.getEditId();
            };
            field->on_change = [this, is_low](const float v) {
                const auto& tree = p_ref_.parameters_;
                if (is_low) {
                    setBand(v, tree.getRawParameterValue(params::kBandHigh)->load(), true);
                } else {
                    setBand(tree.getRawParameterValue(params::kBandLow)->load(), v, false);
                }
            };
            field->on_end = [this] {
                if (editor_.isEditing(field_edit_id_)) {
                    editor_.endEdit();
                }
            };
        }
        updateTooltip();
    }

    // ---------------------------------------------------------------- geometry

    float BandView::freqX(const float hz) const {
        const auto t = std::log(std::clamp(hz, kLowHz, kHighHz) / kLowHz) / std::log(kHighHz / kLowHz);
        return plot_.getX() + plot_.getWidth() * t;
    }

    float BandView::xFreq(const float x) const {
        const auto t = std::clamp((x - plot_.getX()) / plot_.getWidth(), 0.f, 1.f);
        return kLowHz * std::pow(kHighHz / kLowHz, t);
    }

    float BandView::spectrumY(const float db, const float hz) const {
        const auto tilted = db + kTiltDb * std::log2(hz / 1000.f);
        const auto t = std::clamp((kSpectrumTopDb - tilted) / (kSpectrumTopDb - kSpectrumBottomDb), 0.f, 1.f);
        return plot_.getY() + t * plot_.getHeight();
    }

    float BandView::responseY(const float db) const {
        const auto t = kResponseTop + (kResponseBottom - kResponseTop) * std::clamp(-db / kResponseScaleDb, 0.f, 1.2f);
        return plot_.getY() + t * plot_.getHeight();
    }

    float BandView::responseDb(const float hz, const float reduction_db) const {
        if (reduction_db <= 0.f) {
            return 0.f;
        }
        if (wide_) {
            return -reduction_db;
        }
        // the analog prototypes of the engine's shelf and bell
        using C = std::complex<double>;
        const auto g = std::pow(10.0, -reduction_db / 20.0);
        const C s{0.0, static_cast<double>(hz)};
        const auto top = .45 * p_ref_.getEngine().getSampleRate();
        const auto lo = std::min(static_cast<double>(low_), top / 1.3);
        if (!bell_) {
            const auto k = std::sqrt(2.0);
            const auto wp = lo * std::pow(g, .25);
            const auto h = (wp * wp + k * std::sqrt(g) * wp * s + g * s * s) / (s * s + k * wp * s + wp * wp);
            return static_cast<float>(20.0 * std::log10(std::abs(h)));
        }
        const auto hi = std::max(open_ ? std::min(kOpenBellTop, top) : static_cast<double>(high_), lo * 1.26);
        const auto w0 = std::sqrt(lo * hi);
        const auto k = (hi - lo) / w0;
        const auto h = (s * s + g * k * w0 * s + w0 * w0) / (s * s + k * w0 * s + w0 * w0);
        return static_cast<float>(20.0 * std::log10(std::abs(h)));
    }

    juce::RangedAudioParameter* BandView::param(const char* id) const {
        return p_ref_.parameters_.getParameter(id);
    }

    BandView::Target BandView::hit(const juce::Point<float> p) const {
        if (finder_.confidence > 0.f && finder_pill_.expanded(2.f).contains(p)) {
            return Target::kFinder;
        }
        if (!plot_.expanded(0.f, 4.f).contains(p)) {
            return Target::kNone;
        }
        const auto lx = freqX(low_);
        const auto hx = open_ ? plot_.getRight() : freqX(high_);
        const auto dl = std::abs(p.x - lx), dh = std::abs(p.x - hx);
        if (dl <= size::kHandleHitWidth && dl <= dh) {
            return Target::kLow;
        }
        if (dh <= size::kHandleHitWidth) {
            return Target::kHigh;
        }
        if (p.x > lx && p.x < hx) {
            return Target::kBand;
        }
        return Target::kNone;
    }

    int BandView::getHoveredParameterIndex() const {
        const auto t = dragged_ != Target::kNone ? dragged_ : hovered_;
        if (t == Target::kLow) {
            return param(params::kBandLow)->getParameterIndex();
        }
        if (t == Target::kHigh) {
            return param(params::kBandHigh)->getParameterIndex();
        }
        return -1;
    }

    // ---------------------------------------------------------------- state

    void BandView::tick(const double dt, const float reduction_db, const bool new_spectrum) {
        auto dirty = new_spectrum;
        const auto& tree = p_ref_.parameters_;
        const auto low = tree.getRawParameterValue(params::kBandLow)->load();
        const auto high = tree.getRawParameterValue(params::kBandHigh)->load();
        const auto range = tree.getRawParameterValue(params::kRange)->load();
        const auto wide = juce::roundToInt(tree.getRawParameterValue(params::kProcessing)->load()) == 0;
        const auto bell = juce::roundToInt(tree.getRawParameterValue(params::kShape)->load()) == 1;
        low_field_.setValue(low);
        high_field_.setValue(high);
        // drawn as the engine hears it, whatever a host's automation sets
        const auto [band_low, band_high] = params::constrainBand(low, high, true);
        if (band_low != low_ || band_high != high_ || range != range_ || wide != wide_ || bell != bell_) {
            low_ = band_low;
            high_ = band_high;
            open_ = params::isOpen(band_high);
            range_ = range;
            wide_ = wide;
            bell_ = bell;
            dirty = true;
        }
        reduction_.setTarget(reduction_db);
        dirty = reduction_.update(static_cast<float>(dt)) || dirty;
        const auto finder = analyzer_.getFinder();
        if (finder.hz != finder_.hz || finder.confidence != finder_.confidence) {
            finder_ = finder;
            dirty = true;
        }
        finder_fade_.setTarget(finder_.confidence > 0.f);
        dirty = finder_fade_.update(static_cast<float>(dt)) || dirty;
        if (dirty) {
            repaint();
        }
    }

    void BandView::setBand(const float wanted_low, const float wanted_high, const bool low_wins) {
        const auto [low, high] = params::constrainBand(wanted_low, wanted_high, low_wins);
        editor_.setPlain(param(params::kBandLow), std::round(low));
        editor_.setPlain(param(params::kBandHigh), std::round(high));
        low_ = low;
        high_ = high;
        open_ = params::isOpen(high);
        repaint();
    }

    bool BandView::fitToFinder() {
        if (analyzer_.getFinder().confidence <= 0.f) {
            return false;
        }
        const auto hz = analyzer_.getFinder().hz;
        const auto& tree = p_ref_.parameters_;
        float low, high = tree.getRawParameterValue(params::kBandHigh)->load();
        const auto bell = juce::roundToInt(tree.getRawParameterValue(params::kShape)->load()) == 1
                          && juce::roundToInt(tree.getRawParameterValue(params::kProcessing)->load()) == 1;
        if (bell) {
            low = hz / 1.35f;
            high = hz * 1.35f;
        } else {
            // a shelf (or wide band) starting just under the esses
            low = hz / 1.3f;
            if (!params::isOpen(high)) {
                high = std::max(high, hz * 1.35f);
            }
        }
        const auto [fit_low, fit_high] = params::constrainBand(low, high, true);
        editor_.setNow({{param(params::kBandLow), std::round(fit_low)}, {param(params::kBandHigh), std::round(fit_high)}},
                       "Fit Band to Sibilance");
        return true;
    }

    // ---------------------------------------------------------------- painting

    void BandView::resized() {
        auto b = getLocalBounds().toFloat();
        title_area_ = b.removeFromTop(30.f).reduced(size::kS3, 0.f);
        auto title = title_area_;
        // the caption only where there is room for it beside the fields and the finder
        const auto caption_w = juce::GlyphArrangement::getStringWidth(captionFont(), "DETECTION") + size::kS3;
        caption_area_ = title.getWidth() > 420.f ? title.removeFromLeft(caption_w) : juce::Rectangle<float>();
        // fields pad their text by 8: shift them so the labels line up with the plot
        low_field_.setBounds(title.removeFromLeft(98.f).withSizeKeepingCentre(98.f, 22.f).translated(-8.f, 0.f).toNearestInt());
        title.removeFromLeft(size::kS1);
        high_field_.setBounds(title.removeFromLeft(98.f).withSizeKeepingCentre(98.f, 22.f).translated(-8.f, 0.f).toNearestInt());
        plot_ = b.withTrimmedLeft(size::kS3).withTrimmedRight(size::kS3).withTrimmedBottom(20.f);
        rebuildGrid();
    }

    void BandView::rebuildGrid() {
        if (getWidth() <= 0 || getHeight() <= 0) {
            return;
        }
        const auto scale = juce::Component::getApproximateScaleFactorForComponent(this);
        grid_ = juce::Image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(getWidth()) * scale),
                            juce::roundToInt(static_cast<float>(getHeight()) * scale), true);
        juce::Graphics g(grid_);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setColour(colour::kGraph);
        g.fillRoundedRectangle(plot_, size::kRadius);
        static constexpr std::array<float, 10> kLines{300.f, 500.f, 1000.f, 2000.f, 3000.f, 5000.f, 7000.f, 10000.f, 15000.f, 20000.f};
        g.setFont(Fonts::get().font(type::kCaption));
        // labels only where they have room (the minor ones go first in a narrow view)
        const auto roomy = plot_.getWidth() > 420.f;
        for (const auto hz : kLines) {
            const auto x = std::round(freqX(hz));
            const auto major = hz == 1000.f || hz == 10000.f;
            g.setColour(major ? colour::kGridMajor : colour::kGridMinor);
            g.fillRect(juce::Rectangle<float>(x, plot_.getY() + 1.f, 1.f, plot_.getHeight() - 2.f));
            const auto labelled = roomy ? hz != 7000.f && hz != 15000.f : hz != 3000.f && hz != 7000.f && hz != 15000.f && hz != 300.f;
            if (labelled) {
                g.setColour(colour::kTextTertiary);
                g.drawText(shortHz(hz), juce::Rectangle<float>(x - 20.f, plot_.getBottom() + 4.f, 40.f, 14.f),
                           juce::Justification::centred);
            }
        }
        // 0 dB of the gain curve
        juce::Path zero;
        zero.startNewSubPath(plot_.getX(), responseY(0.f));
        zero.lineTo(plot_.getRight(), responseY(0.f));
        const float dashes[] = {2.f, 4.f};
        juce::PathStrokeType(1.f).createDashedStroke(zero, zero, dashes, 2);
        g.setColour(colour::kGridZero);
        g.fillPath(zero);
    }

    juce::Path BandView::spectrumPath(const Analyzer::Curve& curve, const bool closed) const {
        juce::Path path;
        std::array<juce::Point<float>, Analyzer::kPoints> pts;
        for (int i = 0; i < Analyzer::kPoints; ++i) {
            const auto hz = Analyzer::pointHz(i);
            pts[static_cast<size_t>(i)] = {freqX(hz), spectrumY(curve[static_cast<size_t>(i)], hz)};
        }
        const auto bottom = plot_.getBottom() + 1.f;
        if (closed) {
            path.startNewSubPath(pts[0].x, bottom);
            path.lineTo(pts[0]);
        } else {
            path.startNewSubPath(pts[0]);
        }
        for (size_t i = 1; i < pts.size(); ++i) {
            path.quadraticTo(pts[i - 1], (pts[i - 1] + pts[i]) * .5f);
        }
        path.lineTo(pts.back());
        if (closed) {
            path.lineTo(pts.back().x, bottom);
            path.closeSubPath();
        }
        return path;
    }

    void BandView::paintBand(juce::Graphics& g) {
        const auto accent = colour::kAccent;
        const auto lx = freqX(low_);
        const auto hx = open_ ? plot_.getRight() : freqX(high_);
        const auto band = juce::Rectangle<float>(lx, plot_.getY(), hx - lx, plot_.getHeight());
        const auto active = hovered_ == Target::kBand || dragged_ == Target::kBand;
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(active ? .16f : .11f), 0.f, plot_.getY(),
                                               accent.withAlpha(.02f), 0.f, plot_.getBottom(), false));
        g.fillRect(band);

        auto edge = [&](const float x, const Target t, const bool dashed) {
            const auto on = hovered_ == t || dragged_ == t;
            g.setColour(accent.withAlpha(on ? 1.f : .7f));
            if (dashed) {
                juce::Path line;
                line.startNewSubPath(x, plot_.getY());
                line.lineTo(x, plot_.getBottom());
                const float dashes[] = {3.f, 3.f};
                juce::PathStrokeType(1.f).createDashedStroke(line, line, dashes, 2);
                g.fillPath(line);
            } else {
                g.fillRect(juce::Rectangle<float>(x - .75f, plot_.getY(), 1.5f, plot_.getHeight()));
            }
            // a grip halfway down
            const auto grip = juce::Rectangle<float>(on ? 6.f : 5.f, 24.f).withCentre({x, plot_.getCentreY()});
            g.setColour(on ? accent : colour::kTextPrimary.withAlpha(.85f));
            g.fillRoundedRectangle(grip, 2.5f);
            g.setColour(colour::kGraph.withAlpha(.6f));
            g.drawRoundedRectangle(grip.expanded(.5f), 3.f, 1.f);
        };
        edge(lx, Target::kLow, false);
        if (!open_) {
            edge(hx, Target::kHigh, false);
        } else {
            // open: a grip at the far right to pull it in
            edge(plot_.getRight() - 3.f, Target::kHigh, true);
        }
    }

    void BandView::paintResponse(juce::Graphics& g) {
        const auto steps = std::max(2, static_cast<int>(plot_.getWidth() / 2.f));
        auto curve = [&](const float reduction) {
            juce::Path p;
            for (int i = 0; i <= steps; ++i) {
                const auto x = plot_.getX() + plot_.getWidth() * static_cast<float>(i) / static_cast<float>(steps);
                const auto y = responseY(responseDb(xFreq(x), reduction));
                if (i == 0) {
                    p.startNewSubPath(x, y);
                } else {
                    p.lineTo(x, y);
                }
            }
            return p;
        };
        // the most it can do (the range), faint and dashed, so the shape is always visible
        if (range_ > .05f) {
            auto most = curve(range_);
            const float dashes[] = {3.f, 3.f};
            juce::PathStrokeType(1.f).createDashedStroke(most, most, dashes, 2);
            g.setColour(colour::kCurve.withAlpha(.28f));
            g.fillPath(most);
        }
        // what it does now
        const auto now = reduction_.getValue();
        auto live = curve(now);
        if (now > .05f) {
            auto fill = live;
            fill.lineTo(plot_.getRight(), responseY(0.f));
            fill.lineTo(plot_.getX(), responseY(0.f));
            fill.closeSubPath();
            g.setColour(colour::kSibilance.withAlpha(.18f));
            g.fillPath(fill);
        }
        g.setColour(colour::kCurve.withAlpha(.92f));
        g.strokePath(live, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void BandView::paintFinder(juce::Graphics& g) {
        const auto alpha = finder_fade_.getEased();
        if (alpha <= 0.f || finder_.hz <= 0.f) {
            return;
        }
        const auto x = freqX(finder_.hz);
        // a dotted line down to the spectrum, under a pin
        juce::Path line;
        line.startNewSubPath(x, plot_.getY() + 2.f);
        line.lineTo(x, plot_.getBottom());
        const float dots[] = {1.f, 3.f};
        juce::PathStrokeType(1.2f).createDashedStroke(line, line, dots, 2);
        g.setColour(colour::kSibilance.withAlpha(alpha * (.35f + .45f * finder_.confidence)));
        g.fillPath(line);
        const auto on = hovered_ == Target::kFinder;
        g.setColour(colour::kSibilance.withAlpha(alpha));
        g.fillEllipse(juce::Rectangle<float>(7.f, 7.f).withCentre({x, plot_.getY() + 4.f}));
        if (on) {
            g.setColour(colour::kSibilance.withAlpha(.25f * alpha));
            g.fillEllipse(juce::Rectangle<float>(15.f, 15.f).withCentre({x, plot_.getY() + 4.f}));
        }
    }

    void BandView::paint(juce::Graphics& g) {
        g.fillAll(colour::kWindow);
        g.drawImage(grid_, getLocalBounds().toFloat());
        {
            juce::Graphics::ScopedSaveState save(g);
            juce::Path clip;
            clip.addRoundedRectangle(plot_, size::kRadius);
            g.reduceClipRegion(clip);
            paintBand(g);

            // the spectra: what comes in (a soft fill), what goes out (a line), and the esses heard (warm)
            const auto& sibilance = analyzer_.getSibilance();
            if (sibilance[Analyzer::kPoints / 2] > Analyzer::kSilenceDb + 1.f) {
                g.setGradientFill(juce::ColourGradient(colour::kSibilance.withAlpha(.24f), 0.f, plot_.getY(),
                                                       colour::kSibilance.withAlpha(.03f), 0.f, plot_.getBottom(), false));
                g.fillPath(spectrumPath(sibilance, true));
                g.setColour(colour::kSibilance.withAlpha(.55f));
                g.strokePath(spectrumPath(sibilance, false), juce::PathStrokeType(1.f));
            }
            g.setColour(colour::kLevel.withAlpha(.10f));
            g.fillPath(spectrumPath(analyzer_.getPre(), true));
            g.setColour(colour::kLevel.withAlpha(.40f));
            g.strokePath(spectrumPath(analyzer_.getPre(), false), juce::PathStrokeType(1.f));
            g.setColour(colour::kSpectrumPost.withAlpha(.75f));
            g.strokePath(spectrumPath(analyzer_.getPost(), false), juce::PathStrokeType(1.1f));

            paintResponse(g);
            paintFinder(g);
        }

        // the title row: the band's edges (fields, drawn by themselves), and the finder's offer
        if (!caption_area_.isEmpty()) {
            g.setFont(captionFont());
            g.setColour(colour::kTextTertiary);
            g.drawText("DETECTION", caption_area_, juce::Justification::centredLeft);
        }

        finder_pill_ = {};
        if (const auto alpha = finder_fade_.getEased(); alpha > 0.f && finder_.hz > 0.f) {
            // the long form when there is room beside the band's description
            const auto room = title_area_.getRight() - (static_cast<float>(high_field_.getRight()) + size::kS2);
            juce::String text;
            float w = 0.f;
            for (const auto& candidate : {juce::String("Sibilance at ") + params::formatHz(finder_.hz),
                                          params::formatHz(finder_.hz), juce::String(finder_.hz / 1000.f, 1) + "k"}) {
                text = candidate;
                w = juce::GlyphArrangement::getStringWidth(Fonts::get().font(type::kSmall), text) + 30.f;
                if (w <= room) {
                    break;
                }
            }
            finder_pill_ = juce::Rectangle<float>(title_area_.getRight() - w, title_area_.getCentreY() - 10.f, w, 20.f);
            const auto on = hovered_ == Target::kFinder;
            g.setColour(colour::kSibilance.withAlpha(alpha * (on ? .24f : .14f)));
            g.fillRoundedRectangle(finder_pill_, 10.f);
            g.setColour(colour::kSibilance.withAlpha(alpha));
            g.fillEllipse(juce::Rectangle<float>(6.f, 6.f).withCentre({finder_pill_.getX() + 12.f, finder_pill_.getCentreY()}));
            g.setFont(Fonts::get().font(type::kSmall, on ? Fonts::kSemiBold : Fonts::kRegular));
            g.drawText(text, finder_pill_.withTrimmedLeft(21.f).withTrimmedRight(9.f), juce::Justification::centredLeft);
        }
    }

    // ---------------------------------------------------------------- mouse

    void BandView::updateTooltip() {
        switch (hovered_) {
            case Target::kLow: setTooltip("Band low edge: drag to move, double-click to reset");
                break;
            case Target::kHigh: setTooltip(open_ ? "Band high edge (open): drag left to close the band"
                                                 : "Band high edge: drag to the far right to open, double-click to open");
                break;
            case Target::kBand: setTooltip("Drag to move the band; scroll to widen or narrow it");
                break;
            case Target::kFinder: setTooltip("Where this voice's esses are loudest: click to fit the band to them");
                break;
            default: setTooltip({});
                break;
        }
    }

    void BandView::hoverForTesting(const juce::Point<float> p) {
        hovered_ = hit(p);
        repaint();
    }

    void BandView::mouseMove(const juce::MouseEvent& e) {
        if (const auto t = hit(e.position); t != hovered_) {
            hovered_ = t;
            setMouseCursor(t == Target::kLow || t == Target::kHigh ? juce::MouseCursor::LeftRightResizeCursor
                           : t == Target::kBand ? juce::MouseCursor::DraggingHandCursor
                           : t == Target::kFinder ? juce::MouseCursor::PointingHandCursor
                           : juce::MouseCursor::NormalCursor);
            updateTooltip();
            repaint();
        }
    }

    void BandView::mouseExit(const juce::MouseEvent&) {
        if (hovered_ != Target::kNone && dragged_ == Target::kNone) {
            hovered_ = Target::kNone;
            updateTooltip();
            repaint();
        }
    }

    void BandView::mouseDown(const juce::MouseEvent& e) {
        const auto t = hit(e.position);
        if (t == Target::kNone) {
            return;
        }
        if (e.mods.isPopupMenu()) {
            if (t == Target::kLow || t == Target::kHigh) {
                auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
                if (editor != nullptr && editor->getHostContext() != nullptr) {
                    if (auto menu = editor->getHostContext()->getContextMenuForParameter(
                        param(t == Target::kLow ? params::kBandLow : params::kBandHigh))) {
                        menu->showNativeMenu(editor->getLocalPoint(this, e.getPosition()));
                    }
                }
            }
            return;
        }
        if (t == Target::kFinder) {
            fitToFinder();
            return;
        }
        if (wheel_active_) {
            stopTimer();
            timerCallback();
        }
        dragged_ = t;
        drag_low_ = low_;
        drag_high_ = high_;
        drag_start_hz_ = xFreq(e.position.x);
        juce::RangedAudioParameter* paras[] = {param(params::kBandLow), param(params::kBandHigh)};
        editor_.beginEdit(paras, t == Target::kBand ? "Move Band" : t == Target::kLow ? "Change Band Low" : "Change Band High");
        edit_id_ = editor_.getEditId();
        repaint();
    }

    void BandView::mouseDrag(const juce::MouseEvent& e) {
        if (dragged_ == Target::kNone) {
            return;
        }
        // shift: a quarter of the travel
        const auto fine = e.mods.isShiftDown() ? .25f : 1.f;
        const auto ratio = std::pow(xFreq(e.position.x) / drag_start_hz_, fine);
        if (dragged_ == Target::kLow) {
            setBand(drag_low_ * ratio, high_, true);
        } else if (dragged_ == Target::kHigh) {
            // past the last few pixels the band opens
            const auto high = e.position.x >= plot_.getRight() - 4.f ? params::kBandHighMax
                                                                     : std::min(drag_high_ * ratio, params::kBandOpen - 1.f);
            setBand(low_, high, false);
        } else {
            const auto open = params::isOpen(drag_high_);
            auto r = ratio;
            // move as one: stop at whichever edge reaches its end first
            r = std::clamp(r, params::kBandLowMin / drag_low_, params::kBandLowMax / drag_low_);
            if (!open) {
                r = std::clamp(r, params::kBandHighMin / drag_high_, (params::kBandOpen - 1.f) / drag_high_);
            }
            setBand(drag_low_ * r, open ? drag_high_ : drag_high_ * r, true);
        }
    }

    void BandView::mouseUp(const juce::MouseEvent&) {
        if (dragged_ != Target::kNone) {
            if (editor_.isEditing(edit_id_)) {
                editor_.endEdit();
            }
            dragged_ = Target::kNone;
            repaint();
        }
    }

    void BandView::mouseDoubleClick(const juce::MouseEvent& e) {
        switch (hit(e.position)) {
            case Target::kLow: editor_.setNow({{param(params::kBandLow), params::kBandLowDefault}}, "Reset Band Low");
                break;
            case Target::kHigh:
                // toggles: open it, or close it at an octave and a half above the low edge
                editor_.setNow({{param(params::kBandHigh), open_ ? std::min(low_ * 2.8f, params::kBandOpen - 1.f)
                                                                 : params::kBandHighMax}},
                               open_ ? "Close Band" : "Open Band");
                break;
            case Target::kBand:
                editor_.setNow({{param(params::kBandLow), params::kBandLowDefault}, {param(params::kBandHigh), params::kBandHighMax}},
                               "Reset Band");
                break;
            default: break;
        }
    }

    void BandView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
        if (hit(e.position) != Target::kBand || dragged_ != Target::kNone) {
            return;
        }
        if (!wheel_active_) {
            wheel_active_ = true;
            juce::RangedAudioParameter* paras[] = {param(params::kBandLow), param(params::kBandHigh)};
            editor_.beginEdit(paras, "Change Band Width");
            edit_id_ = editor_.getEditId();
        }
        const auto delta = (wheel.deltaY != 0.f ? wheel.deltaY : wheel.deltaX) * (wheel.isReversed ? -1.f : 1.f);
        const auto factor = std::pow(2.f, delta * .5f * ui::InteractionSettings::wheel_sensitivity);
        if (open_) {
            // an open band only has one edge to move
            setBand(low_ / factor, high_, true);
        } else {
            const auto centre = std::sqrt(low_ * high_);
            const auto half = std::max(std::sqrt(params::kMinBandRatio), std::sqrt(high_ / low_) * factor);
            setBand(centre / half, std::min(centre * half, params::kBandOpen - 1.f), true);
        }
        startTimer(450);
    }

    void BandView::timerCallback() {
        stopTimer();
        if (wheel_active_) {
            wheel_active_ = false;
            // another control may have begun its own edit since the last notch: leave that one open
            if (editor_.isEditing(edit_id_)) {
                editor_.endEdit();
            }
        }
    }
}
