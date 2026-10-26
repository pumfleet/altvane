// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "graph_view.hpp"

#include "../model/band_ops.hpp"
#include "../model/session_ops.hpp"
#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"
#include "../util/path_minimizer.hpp"
#include "../util/value_format.hpp"

namespace contour::ui {
    using namespace tokens;
    using zldsp::filter::FilterType;

    namespace {
        constexpr size_t kSoloOff = 2 * kBandNum;

        const std::array<juce::Colour, ResponseModel::kNumStereo> kStereoColours{
            juce::Colour{0xFFF4F6F8}, juce::Colour{0xFF8AB4FF}, juce::Colour{0xFFFF8F8A},
            juce::Colour{0xFFF2D16B}, juce::Colour{0xFFC59BFF}
        };

        const std::array<const char*, ResponseModel::kNumStereo> kStereoNames{"Stereo", "Left", "Right", "Mid", "Side"};

        juce::Font caption() {
            return Fonts::get().font(type::kCaption);
        }

        float clampY(const float y, const float h) {
            return std::clamp(y, -h, 2.f * h);
        }

        double maxFreq(const double sample_rate) {
            return std::min(FreqScale::kMaxFreq, 0.49964 * sample_rate);
        }

        float qFromRatio(const double r) {
            const auto rr = std::max(r, 1.0005);
            return static_cast<float>(std::clamp(std::sqrt(rr) / (rr - 1.0), 0.025, 25.0));
        }

        double halfRatioFromQ(const double q) {
            return (1.0 + std::sqrt(1.0 + 4.0 * q * q)) / (2.0 * q);
        }
    }

    GraphView::GraphView(EngineBridge& bridge, Selection& selection, ResponseModel& response,
                         SpectrumWorker& spectrum) :
        bridge_(bridge), selection_(selection), response_(response), spectrum_(spectrum) {
        for (size_t b = 0; b < kBandNum; ++b) {
            hover_springs_[b] = Spring(0.f, motion::kHoverSpringHz);
            select_springs_[b] = Spring(0.f, motion::kHoverSpringHz);
            appear_springs_[b] = Spring(bridge_.band(b).isActive() ? 1.f : 0.f, 4.f);
        }
        empty_fade_.snap(true);
        setWantsKeyboardFocus(true);
        setOpaque(true);
        setTitle("Frequency response");
    }

    GraphView::~GraphView() {
        stopTimer();
        if (bridge_.isEditing()) {
            bridge_.endEdit();
        }
        setSolo(kBandNum);
    }

    // ---------------------------------------------------------------- geometry

    void GraphView::resized() {
        const auto b = getLocalBounds().toFloat();
        freq_scale_.setBounds(0.f, b.getWidth());
        gain_scale_.setBounds(0.f, b.getHeight(), size::kGraphPadding + 10.f);
        response_.setNumPoints(static_cast<size_t>(std::clamp(b.getWidth(), 256.f, 2048.f)));
        spectrum_.setBounds(b);
        curves_dirty_ = true;
        grid_scale_ = 0.f;
    }

    void GraphView::setBottomInset(const float inset) {
        bottom_inset_ = inset;
    }

    float GraphView::getMaxDB() const {
        return zlstate::PEQMaxDB::kDBs[bridge_.global().eq_max_db_idx];
    }

    juce::Point<float> GraphView::getNodePosition(const size_t band) const {
        const auto& b = bridge_.band(band);
        const auto x = freq_scale_.freqToX(std::min(static_cast<double>(b.freq), FreqScale::kMaxFreq));
        const auto y = gain_scale_.dbToY(band_ops::getNodeDB(b));
        const auto h = static_cast<float>(getHeight());
        return {x, std::clamp(y, 7.f, h - 7.f)};
    }

    juce::Point<float> GraphView::getTargetPosition(const size_t band) const {
        const auto& b = bridge_.band(band);
        const auto x = freq_scale_.freqToX(std::min(static_cast<double>(b.freq), FreqScale::kMaxFreq));
        const auto y = gain_scale_.dbToY(band_ops::getNodeDB(b, true));
        const auto h = static_cast<float>(getHeight());
        return {x, std::clamp(y, 7.f, h - 7.f)};
    }

    float GraphView::getNodeX(const size_t band) const {
        return band < kBandNum ? getNodePosition(band).x : static_cast<float>(getWidth()) * .5f;
    }

    bool GraphView::hasQHandles(const size_t band) const {
        const auto& b = bridge_.band(band);
        return b.type == FilterType::kPeak || b.type == FilterType::kNotch || b.type == FilterType::kBandPass
            || (b.type == FilterType::kAllPass && b.order_idx > 0);
    }

    std::pair<float, float> GraphView::getQHandleXs(const size_t band) const {
        const auto& b = bridge_.band(band);
        const auto k = halfRatioFromQ(b.q);
        return {
            freq_scale_.freqToX(static_cast<double>(b.freq) / k),
            freq_scale_.freqToX(static_cast<double>(b.freq) * k)
        };
    }

    bool GraphView::showsSideChain(const size_t band) const {
        return band < kBandNum && bridge_.band(band).isActive() && bridge_.band(band).dynamic_on;
    }

    GraphView::SideRange GraphView::getSideRange(const size_t band) const {
        const auto& b = bridge_.band(band);
        const auto w = static_cast<float>(getWidth());
        const auto fx = freq_scale_.freqToX(std::min(static_cast<double>(b.side_freq), FreqScale::kMaxFreq));
        switch (b.side_type) {
        case 1: // low pass: detect below the cutoff
            return {0.f, fx, fx, true, false};
        case 2: // high pass: detect above the cutoff
            return {fx, fx, w, true, false};
        case 3: // full range
            return {0.f, w * .5f, w, false, false};
        default: {
            const auto k = halfRatioFromQ(b.side_q);
            return {
                freq_scale_.freqToX(static_cast<double>(b.side_freq) / k), fx,
                freq_scale_.freqToX(static_cast<double>(b.side_freq) * k), true, true
            };
        }
        }
    }

    GraphView::Hit GraphView::hitTestNodes(const juce::Point<float> p) const {
        const auto primary = selection_.getPrimary();
        if (showsSideChain(primary) && std::abs(p.y - kSideBarY) < 9.f) {
            const auto r = getSideRange(primary);
            if (r.has_edges && std::abs(p.x - r.left) < 6.f) {
                return {HitType::kSideLeft, primary};
            }
            if (r.has_edges && std::abs(p.x - r.right) < 6.f) {
                return {HitType::kSideRight, primary};
            }
            if (r.has_centre && std::abs(p.x - r.centre) < 8.f) {
                return {HitType::kSide, primary};
            }
        }
        if (primary < kBandNum && bridge_.band(primary).isActive()) {
            const auto node = getNodePosition(primary);
            if (hasQHandles(primary)) {
                const auto [lx, rx] = getQHandleXs(primary);
                if (p.getDistanceFrom({lx, node.y}) < 7.f && std::abs(lx - node.x) > 10.f) {
                    return {HitType::kQLeft, primary};
                }
                if (p.getDistanceFrom({rx, node.y}) < 7.f && std::abs(rx - node.x) > 10.f) {
                    return {HitType::kQRight, primary};
                }
            }
            const auto& b = bridge_.band(primary);
            if (b.dynamic_on && b.hasGain()) {
                const auto target = getTargetPosition(primary);
                if (p.getDistanceFrom(target) < 7.f && target.getDistanceFrom(node) > 8.f) {
                    return {HitType::kTarget, primary};
                }
            }
            if (p.getDistanceFrom(node) < size::kNodeHitRadius) {
                return {HitType::kNode, primary};
            }
        }
        Hit best;
        float best_d = size::kNodeHitRadius;
        for (size_t i = kBandNum; i-- > 0;) {
            if (!bridge_.band(i).isActive()) {
                continue;
            }
            const auto d = p.getDistanceFrom(getNodePosition(i));
            if (d < best_d) {
                best_d = d;
                best = {HitType::kNode, i};
            }
        }
        return best;
    }

    std::optional<SpectrumWorker::Peak> GraphView::findGrabPeak(const juce::Point<float> p) const {
        if (!grab_enabled_ || bridge_.findOffBand() >= kBandNum) {
            return std::nullopt;
        }
        std::optional<SpectrumWorker::Peak> best;
        float best_d = 14.f;
        for (const auto& peak : spectrum_.getPeaks()) {
            const auto dx = std::abs(peak.x - p.x);
            // generous vertically: the target is the peak's frequency, the pointer is often below the line
            if (dx < best_d && p.y > peak.y - 18.f && p.y < peak.y + 60.f) {
                best_d = dx;
                best = peak;
            }
        }
        return best;
    }

    void GraphView::updateHover(const juce::Point<float> p) {
        hover_hit_ = mouse_inside_ && !isMatching() ? hitTestNodes(p) : Hit{};
        grab_peak_.reset();
        if (mouse_inside_ && hover_hit_.type == HitType::kNone && drag_mode_ == DragMode::kNone) {
            grab_peak_ = findGrabPeak(p);
            if (grab_peak_.has_value()) {
                hover_hit_.type = HitType::kPeak;
            }
        }
        selection_.setHovered(hover_hit_.type == HitType::kNode ? hover_hit_.band : Selection::kNone);
        switch (hover_hit_.type) {
        case HitType::kNode:
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            break;
        case HitType::kQLeft:
        case HitType::kQRight:
        case HitType::kSideLeft:
        case HitType::kSideRight:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case HitType::kSide:
        case HitType::kPeak:
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            break;
        case HitType::kTarget:
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            break;
        case HitType::kNone:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
        }
    }

    // ---------------------------------------------------------------- frame

    void GraphView::tick(const double dt) {
        const auto fdt = static_cast<float>(dt);
        bool needs_repaint = false;

        if (bridge_.getChangedBands().any() || bridge_.haveGlobalsChanged()) {
            selection_.prune(bridge_);
            if (bridge_.haveGlobalsChanged()) {
                gain_scale_.setMaxDB(getMaxDB());
                curves_dirty_ = true;
            }
            if (bridge_.getChangedBands().any()) {
                updateHover(mouse_pos_);
            }
            needs_repaint = true;
        }

        const auto snap = bridge_.isEditing() || wheel_editing_;
        if (response_.update(bridge_, dt, snap)) {
            curves_dirty_ = true;
            needs_repaint = true;
        }
        if (spectrum_.pull()) {
            needs_repaint = true;
            // peaks move with the music; keep the grab target on the nearest one
            if (mouse_inside_ && drag_mode_ == DragMode::kNone
                && (hover_hit_.type == HitType::kNone || hover_hit_.type == HitType::kPeak)) {
                updateHover(mouse_pos_);
            }
        }
        spectrum_.setMouseOver(mouse_inside_);
        if (isMatching() && match_->pullFrame()) {
            needs_repaint = true;
        }
        grab_fade_.setTarget(grab_peak_.has_value());
        needs_repaint = grab_fade_.update(fdt) || needs_repaint;
        if (const auto piano = bridge_.getProcessor().parameters_NA_.getRawParameterValue(
                zlstate::PPianoRollON::kID)->load(std::memory_order::relaxed) > .5f; piano != piano_on_) {
            piano_on_ = piano;
            needs_repaint = true;
        }

        // springs
        bool any_active = false;
        for (size_t b = 0; b < kBandNum; ++b) {
            const auto active = bridge_.band(b).isActive();
            any_active = any_active || active;
            const auto hovered = selection_.getHovered() == b
                || ((drag_mode_ == DragMode::kNode) && b == drag_band_);
            hover_springs_[b].setTarget(hovered ? 1.f : 0.f);
            select_springs_[b].setTarget(selection_.isSelected(b) ? 1.f : 0.f);
            appear_springs_[b].setTarget(active ? 1.f : 0.f);
            needs_repaint = hover_springs_[b].update(fdt) || needs_repaint;
            needs_repaint = select_springs_[b].update(fdt) || needs_repaint;
            needs_repaint = appear_springs_[b].update(fdt) || needs_repaint;
        }
        empty_fade_.setTarget(!any_active);
        needs_repaint = empty_fade_.update(fdt) || needs_repaint;
        solo_spring_.setTarget(solo_band_ < kBandNum ? 1.f : 0.f);
        needs_repaint = solo_spring_.update(fdt) || needs_repaint;

        // readout follows the dragged band immediately, a hovered band after a short delay
        size_t want = kBandNum;
        if (drag_mode_ == DragMode::kNode || drag_mode_ == DragMode::kQLeft || drag_mode_ == DragMode::kQRight
            || drag_mode_ == DragMode::kTarget || drag_mode_ == DragMode::kSide || drag_mode_ == DragMode::kSideLeft
            || drag_mode_ == DragMode::kSideRight) {
            want = drag_band_;
            readout_delay_ = motion::kReadoutDelay;
        } else if (hover_hit_.type != HitType::kNone && hover_hit_.type != HitType::kPeak) {
            want = hover_hit_.band;
            readout_delay_ += fdt;
        } else {
            readout_delay_ = 0.f;
        }
        if (want < kBandNum && readout_delay_ >= motion::kReadoutDelay) {
            readout_band_ = want;
            readout_fade_.setTarget(true);
        } else {
            readout_fade_.setTarget(false);
        }
        needs_repaint = readout_fade_.update(fdt) || needs_repaint;

        if (selection_.getVersion() != last_selection_version_) {
            last_selection_version_ = selection_.getVersion();
            needs_repaint = true;
        }
        if (needs_repaint) {
            repaint();
        }
    }

    void GraphView::rebuildCurves() {
        curves_dirty_ = false;
        const auto n = response_.getNumPoints();
        if (n < 2) {
            return;
        }
        const auto w = static_cast<float>(getWidth());
        const auto h = static_cast<float>(getHeight());
        const auto dx = w / static_cast<float>(n - 1);
        const auto y0 = gain_scale_.getCentreY();

        auto trace = [&](juce::Path& path, const std::vector<float>& dbs, const bool closed) {
            path.clear();
            PathMinimizer<3> minimizer{path};
            if (closed) {
                path.startNewSubPath(0.f, y0);
                minimizer.startNewSubPath<false>(0.f, clampY(gain_scale_.dbToY(dbs[0]), h));
            } else {
                minimizer.startNewSubPath(0.f, clampY(gain_scale_.dbToY(dbs[0]), h));
            }
            for (size_t i = 1; i < n; ++i) {
                minimizer.lineTo(static_cast<float>(i) * dx, clampY(gain_scale_.dbToY(dbs[i]), h));
            }
            minimizer.finish();
            if (closed) {
                path.lineTo(w, y0);
                path.closeSubPath();
            }
        };

        for (size_t b = 0; b < kBandNum; ++b) {
            const auto& band = bridge_.band(b);
            if (!band.isActive() && appear_springs_[b].getValue() < 1e-3f) {
                band_fill_paths_[b].clear();
                band_line_paths_[b].clear();
                range_paths_[b].clear();
                continue;
            }
            const auto& dbs = response_.getBandDB(b);
            trace(band_fill_paths_[b], dbs, true);
            trace(band_line_paths_[b], dbs, false);
            range_paths_[b].clear();
            if (band.dynamic_on && band.isActive()) {
                const auto& lo = response_.getBandStaticDB(b);
                const auto& hi = response_.getBandTargetDB(b);
                auto& p = range_paths_[b];
                p.startNewSubPath(0.f, clampY(gain_scale_.dbToY(lo[0]), h));
                for (size_t i = 1; i < n; ++i) {
                    p.lineTo(static_cast<float>(i) * dx, clampY(gain_scale_.dbToY(lo[i]), h));
                }
                for (size_t i = n; i-- > 0;) {
                    p.lineTo(static_cast<float>(i) * dx, clampY(gain_scale_.dbToY(hi[i]), h));
                }
                p.closeSubPath();
            }
        }
        for (size_t lr = 0; lr < ResponseModel::kNumStereo; ++lr) {
            if (response_.isStereoModeUsed(lr) || lr == 0) {
                trace(sum_paths_[lr], response_.getSumDB(lr), false);
            } else {
                sum_paths_[lr].clear();
            }
        }
    }

    void GraphView::rebuildGrid(const float scale) {
        const auto w = getWidth();
        const auto h = getHeight();
        grid_scale_ = scale;
        grid_db_idx_ = bridge_.global().eq_max_db_idx;
        grid_piano_ = piano_on_;
        grid_spectrum_top_ = zlstate::PFFTTopDB::kDBs[bridge_.global().fft_top_db_idx]
            + 1000.f * static_cast<float>(bridge_.global().fft_min_db_idx);
        grid_image_ = juce::Image(juce::Image::ARGB, std::max(1, juce::roundToInt(static_cast<float>(w) * scale)),
                                  std::max(1, juce::roundToInt(static_cast<float>(h) * scale)), true);
        juce::Graphics g(grid_image_);
        g.addTransform(juce::AffineTransform::scale(scale));
        const auto fw = static_cast<float>(w);
        const auto fh = static_cast<float>(h);

        // frequency lines
        for (double decade = 10.0; decade <= 10000.0; decade *= 10.0) {
            for (int m = 1; m <= 9; ++m) {
                const auto f = decade * m;
                if (f > FreqScale::kMaxFreq) {
                    break;
                }
                const auto x = std::round(freq_scale_.freqToX(f) * scale) / scale;
                g.setColour(m == 1 ? colour::kGridMajor : colour::kGridMinor);
                g.fillRect(juce::Rectangle<float>(x, 0.f, 1.f / scale, fh));
            }
        }
        // EQ scale
        const auto max_db = getMaxDB();
        const auto step = max_db <= 6.f ? 2.f : (max_db <= 12.f ? 3.f : 6.f);
        g.setFont(caption());
        for (float db = -max_db; db <= max_db + 1e-3f; db += step) {
            const auto y = std::round(gain_scale_.dbToY(db) * scale) / scale;
            const auto is_zero = std::abs(db) < 1e-3f;
            g.setColour(is_zero ? colour::kGridZero : colour::kGridMinor.withMultipliedAlpha(1.6f));
            g.fillRect(juce::Rectangle<float>(0.f, y, fw, 1.f / scale));
            const auto text = is_zero ? juce::String("0") : (db > 0 ? "+" : format::kMinus) + juce::String(std::abs(db), 0);
            g.setColour(is_zero ? colour::kTextSecondary : colour::kTextTertiary);
            g.drawText(text, juce::Rectangle<float>(fw - 40.f, y - 12.f, 34.f, 11.f), juce::Justification::centredRight);
        }
        // analyzer scale, quieter, on the left
        const auto top_db = zlstate::PFFTTopDB::kDBs[bridge_.global().fft_top_db_idx];
        const auto range_db = zlstate::PFFTMinDB::kDBs[bridge_.global().fft_min_db_idx];
        const auto top = fh * .04f;
        const auto k = (fh - top) / range_db;
        g.setColour(colour::kTextDisabled);
        for (float db = std::floor(top_db / 12.f) * 12.f - 12.f; db > top_db + range_db + 6.f; db -= 12.f) {
            const auto y = top + (db - top_db) * k;
            if (y > fh - 30.f) {
                break;
            }
            g.drawText(format::kMinus + juce::String(std::abs(db), 0),
                       juce::Rectangle<float>(6.f, y - 6.f, 30.f, 11.f), juce::Justification::centredLeft);
        }
        // frequency labels along the bottom
        g.setColour(colour::kTextTertiary);
        static constexpr std::array<std::pair<double, const char*>, 10> kLabels{
            {
                {20.0, "20"}, {50.0, "50"}, {100.0, "100"}, {200.0, "200"}, {500.0, "500"},
                {1000.0, "1k"}, {2000.0, "2k"}, {5000.0, "5k"}, {10000.0, "10k"}, {20000.0, "20k"}
            }
        };
        const auto label_y = fh - (piano_on_ ? 30.f : 16.f);
        for (const auto& [f, text] : kLabels) {
            const auto x = freq_scale_.freqToX(f);
            g.drawText(text, juce::Rectangle<float>(x - 20.f, label_y, 40.f, 12.f), juce::Justification::centred);
        }
    }

    // ---------------------------------------------------------------- paint

    void GraphView::paint(juce::Graphics& g) {
        const auto paint_start = juce::Time::getMillisecondCounterHiRes();
        g.fillAll(colour::kGraph);
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto spectrum_top = zlstate::PFFTTopDB::kDBs[bridge_.global().fft_top_db_idx]
            + 1000.f * static_cast<float>(bridge_.global().fft_min_db_idx);
        if (std::abs(scale - grid_scale_) > 1e-3f || grid_db_idx_ != bridge_.global().eq_max_db_idx
            || std::abs(spectrum_top - grid_spectrum_top_) > 1e-3f || grid_piano_ != piano_on_) {
            rebuildGrid(scale);
        }
        g.drawImage(grid_image_, getLocalBounds().toFloat());

        paintSpectrum(g);
        paintPiano(g);
        if (curves_dirty_) {
            rebuildCurves();
        }
        paintSolo(g);
        if (isMatching()) {
            // matched bands stay visible underneath as feedback, but quieter
            g.beginTransparencyLayer(.45f);
            paintCurves(g);
            paintNodes(g);
            g.endTransparencyLayer();
            paintMatch(g);
            paintCursor(g);
            return;
        }
        paintCurves(g);
        paintSideChain(g);
        paintGrab(g);
        paintNodes(g);
        paintCursor(g);
        paintReadout(g);
        paintEmptyState(g);

        if (drag_mode_ == DragMode::kLasso) {
            g.setColour(colour::kAccent.withAlpha(.07f));
            g.fillRect(lasso_rect_);
            g.setColour(colour::kAccent.withAlpha(.5f));
            g.drawRect(lasso_rect_, 1.f);
        }

        if (show_perf_) {
            const auto ms = juce::Time::getMillisecondCounterHiRes() - paint_start;
            paint_ms_avg_ += (ms - paint_ms_avg_) * .05;
            paint_ms_max_ = std::max(paint_ms_max_ * .995, ms);
            const auto text = "paint " + juce::String(paint_ms_avg_, 2) + " ms avg  " + juce::String(paint_ms_max_, 2)
                + " ms peak  " + juce::String(getWidth()) + "x" + juce::String(getHeight()) + " @"
                + juce::String(g.getInternalContext().getPhysicalPixelScaleFactor(), 1) + "x";
            auto box = juce::Rectangle<float>(Fonts::get().getTabularWidth(text, type::kCaption) + 14.f, 18.f)
                .withPosition(8.f, 8.f);
            g.setColour(juce::Colours::black.withAlpha(.6f));
            g.fillRoundedRectangle(box, 3.f);
            g.setColour(paint_ms_avg_ > 3.0 ? colour::kWarning : colour::kAccent);
            Fonts::get().drawTabular(g, text, box, juce::Justification::centred, type::kCaption);
        }
    }

    void GraphView::paintSpectrum(juce::Graphics& g) {
        const auto h = static_cast<float>(getHeight());
        if (spectrum_.isSourceOn(SpectrumWorker::kPre)) {
            g.setColour(colour::kSpectrumPre.withAlpha(.55f));
            g.strokePath(spectrum_.getPaths(SpectrumWorker::kPre).line, juce::PathStrokeType(1.f));
        }
        if (spectrum_.isSourceOn(SpectrumWorker::kSide)) {
            const auto& p = spectrum_.getPaths(SpectrumWorker::kSide);
            g.setColour(colour::kSpectrumSide.withAlpha(.10f));
            g.fillPath(p.fill);
            g.setColour(colour::kSpectrumSide.withAlpha(.5f));
            g.strokePath(p.line, juce::PathStrokeType(1.f));
        }
        if (spectrum_.isSourceOn(SpectrumWorker::kPost)) {
            const auto& p = spectrum_.getPaths(SpectrumWorker::kPost);
            g.setGradientFill(juce::ColourGradient(colour::kSpectrumPost.withAlpha(.26f), 0.f, h * .15f,
                                                   colour::kSpectrumPost.withAlpha(.03f), 0.f, h, false));
            g.fillPath(p.fill);
            g.setColour(colour::kSpectrumPost.withAlpha(.42f));
            g.strokePath(p.line, juce::PathStrokeType(1.f));
        }
        if (spectrum_.isCollisionOn() && spectrum_.getCollision().getNumColours() >= 2) {
            // masked regions light up inside the output spectrum itself
            g.saveState();
            g.reduceClipRegion(spectrum_.getPaths(SpectrumWorker::kPost).fill);
            g.setGradientFill(spectrum_.getCollision());
            g.setOpacity(.55f);
            g.fillRect(getLocalBounds().toFloat());
            g.restoreState();
        }
    }

    void GraphView::paintCurves(juce::Graphics& g) {
        const auto h = static_cast<float>(getHeight());
        const auto y0 = gain_scale_.getCentreY();
        const auto primary = selection_.getPrimary();
        const auto hovered = selection_.getHovered();

        auto paint_band = [&](const size_t b) {
            const auto& band = bridge_.band(b);
            const auto appear = appear_springs_[b].getValue();
            if (appear < 1e-3f || band_fill_paths_[b].isEmpty()) {
                return;
            }
            const auto c = colour::band(b);
            const auto emphasis = std::max(hover_springs_[b].getValue() * .7f, select_springs_[b].getValue());
            auto alpha = (band.isOn() ? .085f : .04f) + emphasis * .13f;
            alpha *= appear;
            juce::ColourGradient grad(c.withAlpha(alpha), 0.f, y0 - gain_scale_.getHalfHeight(),
                                      c.withAlpha(alpha), 0.f, y0 + gain_scale_.getHalfHeight(), false);
            grad.addColour(.5, c.withAlpha(alpha * .3f));
            g.setGradientFill(grad);
            g.fillPath(band_fill_paths_[b]);
            if (!range_paths_[b].isEmpty()) {
                g.setColour(c.withAlpha((.07f + .1f * emphasis) * appear));
                g.fillPath(range_paths_[b]);
            }
            if (emphasis > .01f) {
                g.setColour(c.withAlpha(.85f * emphasis * appear));
                if (band.isOn()) {
                    g.strokePath(band_line_paths_[b], juce::PathStrokeType(size::kBandStroke));
                } else {
                    juce::Path dashed;
                    const float dashes[] = {4.f, 3.f};
                    juce::PathStrokeType(size::kBandStroke).createDashedStroke(dashed, band_line_paths_[b], dashes, 2);
                    g.fillPath(dashed);
                }
            }
        };

        for (size_t b = 0; b < kBandNum; ++b) {
            if (b != primary && b != hovered) {
                paint_band(b);
            }
        }
        if (hovered < kBandNum && hovered != primary) {
            paint_band(hovered);
        }
        if (primary < kBandNum) {
            paint_band(primary);
        }

        // composite curves; with mixed stereo placement each placement gets its own colour
        const auto primary_lr = primary < kBandNum ? static_cast<size_t>(bridge_.band(primary).lr) : 0;
        for (size_t lr = ResponseModel::kNumStereo; lr-- > 0;) {
            if (sum_paths_[lr].isEmpty() || (lr > 0 && !response_.isStereoModeUsed(lr))) {
                continue;
            }
            const auto emphasised = lr == primary_lr;
            // a flat line with no bands is a reference, not content
            const auto c = kStereoColours[lr].withMultipliedAlpha(1.f - .6f * empty_fade_.getEased());
            g.setColour(c.withAlpha(emphasised ? .07f : .03f));
            g.strokePath(sum_paths_[lr], juce::PathStrokeType(5.f, juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));
            g.setColour(c.withAlpha(emphasised ? .95f : .55f));
            g.strokePath(sum_paths_[lr], juce::PathStrokeType(size::kCurveStroke, juce::PathStrokeType::curved,
                                                              juce::PathStrokeType::rounded));
        }
        juce::ignoreUnused(h);
    }

    void GraphView::paintNodes(juce::Graphics& g) {
        const auto primary = selection_.getPrimary();
        const auto number_font = Fonts::get().font(9.f, Fonts::kSemiBold);

        // Q handles and dynamic target of the primary band
        if (primary < kBandNum && bridge_.band(primary).isActive()) {
            const auto& b = bridge_.band(primary);
            const auto c = colour::band(primary);
            const auto node = getNodePosition(primary);
            const auto sel = select_springs_[primary].getValue();
            if (hasQHandles(primary)) {
                const auto [lx, rx] = getQHandleXs(primary);
                for (const auto [x, type] : {std::pair{lx, HitType::kQLeft}, std::pair{rx, HitType::kQRight}}) {
                    if (std::abs(x - node.x) < 10.f) {
                        continue;
                    }
                    const auto hot = hover_hit_.type == type
                        || (drag_mode_ == DragMode::kQLeft && type == HitType::kQLeft)
                        || (drag_mode_ == DragMode::kQRight && type == HitType::kQRight);
                    g.setColour(c.withAlpha((hot ? .35f : .18f) * sel));
                    g.drawLine(node.x, node.y, x, node.y, 1.f);
                    const auto r = juce::Rectangle<float>(hot ? 4.f : 3.f, hot ? 12.f : 10.f).withCentre({x, node.y});
                    g.setColour(colour::kGraph);
                    g.fillRoundedRectangle(r.expanded(1.5f), 3.f);
                    g.setColour(c.withAlpha((hot ? 1.f : .75f) * sel));
                    g.fillRoundedRectangle(r, 2.f);
                }
            }
            if (b.dynamic_on && b.hasGain()) {
                const auto target = getTargetPosition(primary);
                if (target.getDistanceFrom(node) > 8.f) {
                    juce::Path line;
                    line.startNewSubPath(node);
                    line.lineTo(target);
                    juce::Path dashed;
                    const float dashes[] = {2.f, 3.f};
                    juce::PathStrokeType(1.f).createDashedStroke(dashed, line, dashes, 2);
                    g.setColour(c.withAlpha(.6f * sel));
                    g.fillPath(dashed);
                    const auto hot = hover_hit_.type == HitType::kTarget || drag_mode_ == DragMode::kTarget;
                    const auto r = hot ? 5.5f : 4.5f;
                    g.setColour(colour::kGraph);
                    g.fillEllipse(juce::Rectangle<float>(2.f * r, 2.f * r).withCentre(target));
                    g.setColour(c.withAlpha(sel));
                    g.drawEllipse(juce::Rectangle<float>(2.f * r, 2.f * r).withCentre(target), 1.5f);
                }
                // the live gain, as a small tick riding between node and target
                const auto delta = response_.getDynamicDelta(primary);
                if (std::abs(delta) > .05f) {
                    auto live = b;
                    live.gain += delta;
                    const auto live_db = band_ops::getNodeDB(live);
                    const auto y = std::clamp(gain_scale_.dbToY(live_db), 7.f, static_cast<float>(getHeight()) - 7.f);
                    g.setColour(c.withAlpha(.9f));
                    g.fillRoundedRectangle(juce::Rectangle<float>(12.f, 2.f).withCentre({node.x, y}), 1.f);
                }
            }
        }

        auto paint_node = [&](const size_t i) {
            const auto& b = bridge_.band(i);
            const auto appear = appear_springs_[i].getValue();
            if (!b.isActive() || appear < 1e-3f) {
                return;
            }
            const auto c = colour::band(i);
            const auto p = getNodePosition(i);
            const auto hover = hover_springs_[i].getValue();
            const auto sel = select_springs_[i].getValue();
            const auto r = size::kNodeRadius * (1.f + .18f * hover) * (.6f + .4f * appear);

            if (hover > .01f) {
                const auto halo_r = r * 2.8f;
                g.setGradientFill(juce::ColourGradient(c.withAlpha(.28f * hover), p.x, p.y,
                                                       c.withAlpha(0.f), p.x + halo_r, p.y, true));
                g.fillEllipse(juce::Rectangle<float>(2.f * halo_r, 2.f * halo_r).withCentre(p));
            }
            if (sel > .01f) {
                const auto ring_r = r + 3.5f;
                g.setColour(c.withAlpha(.65f * sel));
                g.drawEllipse(juce::Rectangle<float>(2.f * ring_r, 2.f * ring_r).withCentre(p), 1.4f);
            }
            const auto body = juce::Rectangle<float>(2.f * r, 2.f * r).withCentre(p);
            // separation ring keeps nodes legible over curves and spectrum
            g.setColour(colour::kGraph.withAlpha(.85f));
            g.fillEllipse(body.expanded(1.5f));
            if (b.isOn()) {
                g.setColour(c.withMultipliedAlpha(appear));
                g.fillEllipse(body);
                g.setColour(colour::kGraph.withAlpha(.92f));
            } else {
                g.setColour(c.withAlpha(.7f * appear));
                g.drawEllipse(body.reduced(.75f), 1.5f);
                g.setColour(c.withAlpha(.9f * appear));
            }
            g.setFont(number_font);
            g.drawText(juce::String(i + 1), body.translated(0.f, .5f), juce::Justification::centred, false);
            if (b.dynamic_on) {
                // small notch marks a dynamic band even when it is not selected
                g.setColour(c.withAlpha(.8f * appear));
                g.fillEllipse(juce::Rectangle<float>(3.f, 3.f).withCentre({p.x + r + 2.5f, p.y - r - 1.f}));
            }
        };
        for (size_t i = 0; i < kBandNum; ++i) {
            if (!selection_.isSelected(i) && i != selection_.getHovered()) {
                paint_node(i);
            }
        }
        for (size_t i = 0; i < kBandNum; ++i) {
            if (selection_.isSelected(i) && i != primary) {
                paint_node(i);
            }
        }
        if (selection_.getHovered() < kBandNum && selection_.getHovered() != primary) {
            paint_node(selection_.getHovered());
        }
        if (primary < kBandNum) {
            paint_node(primary);
        }
    }

    void GraphView::paintReadout(juce::Graphics& g) {
        const auto alpha = readout_fade_.getEased();
        if (alpha < .01f || readout_band_ >= kBandNum || !bridge_.band(readout_band_).isActive()) {
            return;
        }
        const auto& b = bridge_.band(readout_band_);
        const auto p = getNodePosition(readout_band_);

        const auto line1 = format::freq(b.freq);
        const auto note = format::note(b.freq);
        juce::String line2;
        const auto side_drag = drag_mode_ == DragMode::kSide || drag_mode_ == DragMode::kSideLeft
            || drag_mode_ == DragMode::kSideRight
            || (drag_mode_ == DragMode::kNone && (hover_hit_.type == HitType::kSide || hover_hit_.type == HitType::kSideLeft
                                                  || hover_hit_.type == HitType::kSideRight));
        if (side_drag) {
            line2 = "Detector " + format::freq(b.side_freq);
            if (b.side_type == 0) {
                line2 << "   Q " << format::q(b.side_q);
            }
        } else if (drag_mode_ == DragMode::kTarget) {
            line2 = "Range " + format::gain(b.target_gain - b.gain);
        } else if (b.hasGain()) {
            line2 = format::gain(b.gain);
            if (b.hasQ()) {
                line2 << "   Q " << format::q(b.q);
            }
        } else {
            line2 = b.hasQ() ? "Q " + format::q(b.q) : juce::String();
            line2 << (line2.isEmpty() ? "" : "   ") << zlp::POrder::kChoices[static_cast<int>(b.order_idx)];
        }
        auto& fonts = Fonts::get();
        const auto w1 = fonts.getTabularWidth(line1, type::kBody, Fonts::kSemiBold)
            + juce::GlyphArrangement::getStringWidth(caption(), note) + 8.f;
        const auto w2 = fonts.getTabularWidth(line2, type::kSmall);
        const auto box_w = std::max(w1, w2) + 20.f;
        constexpr auto box_h = 38.f;
        auto box = juce::Rectangle<float>(box_w, box_h).withCentre({p.x, p.y - 34.f});
        if (box.getY() < 4.f) {
            box.setY(p.y + 20.f);
        }
        box = box.constrainedWithin(getLocalBounds().toFloat().reduced(6.f));

        g.setColour(juce::Colours::black.withAlpha(.25f * alpha));
        g.fillRoundedRectangle(box.translated(0.f, 2.f).expanded(1.f), size::kRadius);
        g.setColour(colour::kRaised.withAlpha(.96f * alpha));
        g.fillRoundedRectangle(box, size::kRadius);
        g.setColour(colour::band(readout_band_).withAlpha(.35f * alpha));
        g.drawRoundedRectangle(box.reduced(.5f), size::kRadius, 1.f);

        auto inner = box.reduced(10.f, 5.f);
        auto top = inner.removeFromTop(15.f);
        g.setColour(colour::kTextPrimary.withAlpha(.95f * alpha));
        fonts.drawTabular(g, line1, top, juce::Justification::centredLeft, type::kBody, Fonts::kSemiBold);
        g.setColour(colour::kTextTertiary.withMultipliedAlpha(alpha));
        g.setFont(caption());
        g.drawText(note, top, juce::Justification::centredRight);
        g.setColour(colour::kTextSecondary.withMultipliedAlpha(alpha));
        fonts.drawTabular(g, line2, inner, juce::Justification::centredLeft, type::kSmall);
    }

    void GraphView::paintCursor(juce::Graphics& g) {
        if (!mouse_inside_ || hover_hit_.type != HitType::kNone || drag_mode_ != DragMode::kNone) {
            return;
        }
        const auto h = static_cast<float>(getHeight());
        g.setColour(juce::Colours::white.withAlpha(.05f));
        g.fillRect(juce::Rectangle<float>(mouse_pos_.x, 0.f, 1.f, h));
        const auto f = freq_scale_.xToFreq(mouse_pos_.x);
        const auto db = gain_scale_.yToDB(mouse_pos_.y);
        const auto text = format::freq(f) + "  " + format::gain(db, true, true);
        auto& fonts = Fonts::get();
        const auto w = fonts.getTabularWidth(text, type::kCaption) + 12.f;
        auto box = juce::Rectangle<float>(w, 16.f).withPosition(mouse_pos_.x + 10.f, mouse_pos_.y + 14.f);
        if (box.getRight() > static_cast<float>(getWidth()) - 4.f) {
            box.setX(mouse_pos_.x - w - 10.f);
        }
        g.setColour(colour::kRaised.withAlpha(.85f));
        g.fillRoundedRectangle(box, 3.f);
        g.setColour(colour::kTextSecondary);
        fonts.drawTabular(g, text, box, juce::Justification::centred, type::kCaption);
    }

    void GraphView::paintSolo(juce::Graphics& g) {
        const auto amount = solo_spring_.getValue();
        if (amount < .01f || solo_band_ >= kBandNum) {
            return;
        }
        const auto& b = bridge_.band(solo_band_);
        float lx, rx;
        const auto fx = freq_scale_.freqToX(b.freq);
        if (solo_side_) {
            const auto r = getSideRange(solo_band_);
            lx = r.left;
            rx = r.right;
        } else if (b.type == FilterType::kHighPass || b.type == FilterType::kLowShelf) {
            lx = 0.f;
            rx = fx;
        } else if (b.type == FilterType::kLowPass || b.type == FilterType::kHighShelf) {
            lx = fx;
            rx = static_cast<float>(getWidth());
        } else {
            const auto k = halfRatioFromQ(std::max(b.q, .1f));
            lx = freq_scale_.freqToX(b.freq / k);
            rx = freq_scale_.freqToX(b.freq * k);
        }
        const auto h = static_cast<float>(getHeight());
        g.setColour(juce::Colours::black.withAlpha(.45f * amount));
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, std::max(lx, 0.f), h));
        g.fillRect(juce::Rectangle<float>(rx, 0.f, std::max(static_cast<float>(getWidth()) - rx, 0.f), h));
        g.setColour(colour::band(solo_band_).withAlpha(.05f * amount));
        g.fillRect(juce::Rectangle<float>(lx, 0.f, rx - lx, h));
    }

    void GraphView::paintSideChain(juce::Graphics& g) {
        const auto primary = selection_.getPrimary();
        if (!showsSideChain(primary)) {
            return;
        }
        const auto& b = bridge_.band(primary);
        const auto sel = select_springs_[primary].getValue();
        const auto c = colour::band(primary);
        const auto r = getSideRange(primary);
        const auto hot = [this](const HitType t, const DragMode m) {
            return hover_hit_.type == t || drag_mode_ == m;
        };
        // caption, tucked to the left of the bar unless the bar starts at the edge
        g.setFont(Fonts::get().font(9.f, Fonts::kSemiBold).withExtraKerningFactor(.08f));
        g.setColour(colour::kTextTertiary.withMultipliedAlpha(sel));
        const auto caption_x = r.left > 70.f ? r.left - 62.f : r.right + 8.f;
        g.drawText(b.side_link ? juce::String(juce::CharPointer_UTF8("DETECT \xc2\xb7 LINK")) : juce::String("DETECT"),
                   juce::Rectangle<float>(caption_x, kSideBarY - 6.f, 60.f, 12.f),
                   r.left > 70.f ? juce::Justification::centredRight : juce::Justification::centredLeft);
        // range
        auto bar = juce::Rectangle<float>(r.left, kSideBarY - 2.f, std::max(r.right - r.left, 2.f), 4.f);
        g.setColour(c.withAlpha(.12f * sel));
        g.fillRect(juce::Rectangle<float>(r.left, kSideBarY + 2.f, bar.getWidth(), static_cast<float>(getHeight())));
        g.setColour(c.withAlpha((hot(HitType::kSide, DragMode::kSide) ? .7f : .45f) * sel));
        g.fillRoundedRectangle(bar, 2.f);
        if (r.has_edges) {
            for (const auto [x, t, m] : {std::tuple{r.left, HitType::kSideLeft, DragMode::kSideLeft},
                                        std::tuple{r.right, HitType::kSideRight, DragMode::kSideRight}}) {
                const auto h = hot(t, m);
                g.setColour(c.withAlpha((h ? 1.f : .7f) * sel));
                g.fillRoundedRectangle(juce::Rectangle<float>(h ? 3.f : 2.f, h ? 14.f : 11.f).withCentre({x, kSideBarY}),
                                       1.f);
            }
        }
        if (r.has_centre) {
            const auto h = hot(HitType::kSide, DragMode::kSide);
            const auto rad = h ? 5.f : 4.f;
            const auto dot = juce::Rectangle<float>(2.f * rad, 2.f * rad).withCentre({r.centre, kSideBarY});
            g.setColour(colour::kGraph);
            g.fillEllipse(dot.expanded(1.5f));
            g.setColour(c.withAlpha(sel));
            g.drawEllipse(dot, 1.5f);
        }
    }

    void GraphView::dragSide(const float x, const DragMode mode) {
        const auto& b = bridge_.band(drag_band_);
        const auto& bp = bridge_.params(drag_band_);
        bridge_.setPlain(bp[BandParams::kSideLink], 0.f);
        const auto f = std::clamp(freq_scale_.xToFreq(x), FreqScale::kMinFreq, maxFreq(bridge_.getSampleRate()));
        if (mode == DragMode::kSide) {
            bridge_.setPlain(bp[BandParams::kSideFreq], static_cast<float>(f));
        } else {
            const auto half = mode == DragMode::kSideLeft ? static_cast<double>(b.side_freq) / f
                                                          : f / static_cast<double>(b.side_freq);
            bridge_.setPlain(bp[BandParams::kSideQ], qFromRatio(half * half));
        }
    }

    void GraphView::paintGrab(juce::Graphics& g) {
        const auto alpha = grab_fade_.getEased();
        if (alpha < .01f || !grab_peak_.has_value()) {
            return;
        }
        const auto& peak = *grab_peak_;
        const auto h = static_cast<float>(getHeight());
        g.setColour(colour::kAccent.withAlpha(.12f * alpha));
        g.fillRect(juce::Rectangle<float>(peak.x - .5f, peak.y, 1.f, h - peak.y));
        const auto ring = juce::Rectangle<float>(12.f, 12.f).withCentre({peak.x, peak.y});
        g.setColour(colour::kAccent.withAlpha(.25f * alpha));
        g.fillEllipse(ring.expanded(4.f));
        g.setColour(colour::kAccent.withAlpha(alpha));
        g.drawEllipse(ring, 1.5f);
        const auto text = format::freq(peak.freq) + "  " + format::note(peak.freq).upToFirstOccurrenceOf(" ", false, false);
        auto& fonts = Fonts::get();
        const auto w = fonts.getTabularWidth(text, type::kCaption) + 14.f;
        auto box = juce::Rectangle<float>(w, 18.f).withCentre({peak.x, peak.y - 20.f});
        box = box.constrainedWithin(getLocalBounds().toFloat().reduced(4.f));
        g.setColour(colour::kRaised.withAlpha(.92f * alpha));
        g.fillRoundedRectangle(box, 3.f);
        g.setColour(colour::kAccent.withAlpha(alpha));
        fonts.drawTabular(g, text, box, juce::Justification::centred, type::kCaption);
    }

    void GraphView::paintPiano(juce::Graphics& g) {
        if (!piano_on_) {
            return;
        }
        const auto h = static_cast<float>(getHeight());
        const auto strip = juce::Rectangle<float>(0.f, h - 14.f, static_cast<float>(getWidth()), 14.f);
        g.setColour(juce::Colour{0xFF1B1F24});
        g.fillRect(strip);
        std::vector<std::pair<float, int>> labels;
        // semitones from A0 (27.5 Hz) to C10
        for (int n = -9; n <= 87 + 12; ++n) {
            const auto f_lo = 440.0 * std::exp2((n - 48 - .5) / 12.0);
            const auto f_hi = 440.0 * std::exp2((n - 48 + .5) / 12.0);
            const auto x0 = freq_scale_.freqToX(f_lo);
            const auto x1 = freq_scale_.freqToX(f_hi);
            if (x1 < 0.f || x0 > strip.getRight()) {
                continue;
            }
            const auto note = ((n % 12) + 12) % 12; // 0 = A
            const auto black = note == 1 || note == 4 || note == 6 || note == 9 || note == 11;
            if (black) {
                g.setColour(juce::Colour{0xFF0D0F12});
                g.fillRect(juce::Rectangle<float>(x0, strip.getY(), x1 - x0, strip.getHeight() * .5f));
            }
            if (note == 3) { // C
                g.setColour(colour::kHairlineStrong);
                g.fillRect(juce::Rectangle<float>(x0, strip.getY(), 1.f, strip.getHeight()));
                labels.emplace_back(x0, (n + 9) / 12);
            }
        }
        // octave labels sit under the black keys, drawn last so no key covers them
        g.setColour(colour::kTextTertiary);
        g.setFont(Fonts::get().font(8.f));
        for (const auto& [x, octave] : labels) {
            g.drawText("C" + juce::String(octave), juce::Rectangle<float>(x + 2.f, strip.getBottom() - 8.f, 20.f, 8.f),
                       juce::Justification::centredLeft);
        }
        // highlight the hovered note
        if (mouse_inside_) {
            const auto f = freq_scale_.xToFreq(mouse_pos_.x);
            const auto n = std::round(12.0 * std::log2(f / 440.0));
            const auto x0 = freq_scale_.freqToX(440.0 * std::exp2((n - .5) / 12.0));
            const auto x1 = freq_scale_.freqToX(440.0 * std::exp2((n + .5) / 12.0));
            g.setColour(colour::kAccent.withAlpha(.5f));
            g.fillRect(juce::Rectangle<float>(x0, strip.getY(), std::max(x1 - x0, 1.f), strip.getHeight()));
        }
    }

    size_t GraphView::matchIndexAt(const float x) const {
        const auto& frame = match_->getFrame();
        const auto f0 = static_cast<double>(frame.freqs.front());
        const auto f1 = static_cast<double>(frame.freqs.back());
        if (f1 <= f0) {
            return 0;
        }
        const auto p = std::log(std::max(freq_scale_.xToFreq(x), f0) / f0) / std::log(f1 / f0);
        return static_cast<size_t>(std::clamp(std::round(p * (MatchEngine::kNumPoints - 1)), 0.0,
                                              static_cast<double>(MatchEngine::kNumPoints - 1)));
    }

    void GraphView::matchDraw(const juce::Point<float> p, const bool start) {
        const auto idx = matchIndexAt(p.x);
        const auto db = std::clamp(gain_scale_.yToDB(p.y), -30.f, 30.f);
        if (start) {
            match_last_idx_ = idx;
            match_last_db_ = db;
        }
        if (match_erasing_) {
            match_->eraseSegment(match_last_idx_, idx);
        } else {
            match_->drawSegment(match_last_idx_, match_last_db_, idx, db);
        }
        match_last_idx_ = idx;
        match_last_db_ = db;
    }

    void GraphView::paintMatch(juce::Graphics& g) {
        const auto& frame = match_->getFrame();
        if (!frame.valid) {
            return;
        }
        const auto fh = static_cast<float>(getHeight());
        const auto top_db = zlstate::PFFTTopDB::kDBs[bridge_.global().fft_top_db_idx];
        const auto range_db = zlstate::PFFTMinDB::kDBs[bridge_.global().fft_min_db_idx];
        const auto top = fh * .04f;
        const auto k = (fh - top) / range_db;
        auto spectrum_y = [&](const float db) { return std::clamp(top + (db - top_db) * k, -fh, 2.f * fh); };
        auto build = [&](juce::Path& path, const std::array<float, MatchEngine::kNumPoints>& dbs, const bool eq_scale) {
            path.clear();
            for (size_t i = 0; i < MatchEngine::kNumPoints; ++i) {
                const auto x = freq_scale_.freqToX(frame.freqs[i]);
                const auto y = eq_scale ? clampY(gain_scale_.dbToY(dbs[i]), fh) : spectrum_y(dbs[i]);
                if (i == 0) {
                    path.startNewSubPath(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
        };
        build(match_source_, frame.source, false);
        build(match_target_, frame.target, false);
        build(match_correction_, frame.correction, true);
        const juce::PathStrokeType thin(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(colour::kSpectrumPost.withAlpha(.8f));
        g.strokePath(match_source_, thin);
        g.setColour(colour::kSpectrumSide.withAlpha(.85f));
        juce::Path dashed;
        const float dashes[] = {5.f, 4.f};
        thin.createDashedStroke(dashed, match_target_, dashes, 2);
        g.fillPath(dashed);

        // the correction, on the EQ scale
        match_fill_ = match_correction_;
        match_fill_.lineTo(freq_scale_.freqToX(frame.freqs.back()), gain_scale_.getCentreY());
        match_fill_.lineTo(freq_scale_.freqToX(frame.freqs.front()), gain_scale_.getCentreY());
        match_fill_.closeSubPath();
        g.setColour(colour::kAccent.withAlpha(.12f));
        g.fillPath(match_fill_);
        g.setColour(colour::kAccent.withAlpha(.12f));
        g.strokePath(match_correction_, juce::PathStrokeType(6.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour::kAccent);
        g.strokePath(match_correction_, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // legend
        auto legend = juce::Rectangle<float>(12.f, 10.f, 360.f, 14.f);
        g.setFont(Fonts::get().font(type::kCaption, Fonts::kSemiBold));
        for (const auto& [name, c] : {std::pair{"SOURCE", colour::kSpectrumPost}, std::pair{"REFERENCE", colour::kSpectrumSide},
                                     std::pair{"CORRECTION", colour::kAccent}}) {
            g.setColour(c);
            g.fillRoundedRectangle(legend.removeFromLeft(10.f).withSizeKeepingCentre(10.f, 3.f), 1.5f);
            legend.removeFromLeft(6.f);
            const auto w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name) + 16.f;
            g.setColour(colour::kTextSecondary);
            g.drawText(name, legend.removeFromLeft(w), juce::Justification::centredLeft);
        }
    }

    void GraphView::paintEmptyState(juce::Graphics& g) {
        const auto alpha = isMatching() ? 0.f : empty_fade_.getEased();
        if (alpha < .01f) {
            return;
        }
        // sits above the flat 0 dB line
        auto area = juce::Rectangle<float>(420.f, 44.f).withCentre({
            static_cast<float>(getWidth()) * .5f, gain_scale_.getCentreY() - 44.f
        });
        g.setColour(colour::kTextSecondary.withMultipliedAlpha(alpha));
        g.setFont(Fonts::get().font(type::kTitle));
        g.drawText("Double-click to add a band", area.removeFromTop(22.f), juce::Justification::centred);
        g.setColour(colour::kTextTertiary.withMultipliedAlpha(alpha));
        g.setFont(Fonts::get().font(type::kSmall));
        g.drawText(juce::String(juce::CharPointer_UTF8(
                       "Drag nodes to shape  \xc2\xb7  Scroll for Q  \xc2\xb7  Right-click for more")),
                   area, juce::Justification::centred);
    }

    // ---------------------------------------------------------------- editing

    std::vector<size_t> GraphView::getSelectedBands() const {
        std::vector<size_t> result;
        for (size_t b = 0; b < kBandNum; ++b) {
            if (selection_.isSelected(b) && bridge_.band(b).isActive()) {
                result.push_back(b);
            }
        }
        return result;
    }

    void GraphView::beginNodeDrag(const size_t band) {
        drag_band_ = band;
        drag_starts_.clear();
        std::vector<juce::RangedAudioParameter*> paras;
        for (const auto b : getSelectedBands()) {
            const auto& s = bridge_.band(b);
            drag_starts_.push_back({b, s.freq, band_ops::getNodeDB(s), s.q, s.target_gain, s.gain});
            const auto& bp = bridge_.params(b);
            paras.push_back(bp[BandParams::kFreq]);
            paras.push_back(bp[BandParams::kGain]);
            paras.push_back(bp[BandParams::kTargetGain]);
            paras.push_back(bp[BandParams::kQ]);
        }
        bridge_.beginEdit(paras, drag_starts_.size() > 1 ? "Move Bands" : "Move Band");
    }

    void GraphView::dragNodes(juce::Point<float> delta, const bool constrain) {
        if (constrain) {
            if (axis_lock_ == 0 && delta.getDistanceFromOrigin() > 6.f) {
                axis_lock_ = std::abs(delta.x) >= std::abs(delta.y) ? 1 : 2;
            }
            if (axis_lock_ == 1) {
                delta.y = 0.f;
            } else if (axis_lock_ == 2) {
                delta.x = 0.f;
            } else {
                delta = {};
            }
        } else {
            axis_lock_ = 0;
        }
        const auto max_f = maxFreq(bridge_.getSampleRate());
        const auto lead = std::find_if(drag_starts_.begin(), drag_starts_.end(),
                                       [this](const DragStart& d) { return d.band == drag_band_; });
        if (lead == drag_starts_.end()) {
            return;
        }
        const auto lead_x = freq_scale_.freqToX(lead->freq) + delta.x;
        const auto lead_f = std::clamp(freq_scale_.xToFreq(lead_x), FreqScale::kMinFreq, max_f);
        const auto ratio = lead_f / static_cast<double>(lead->freq);
        const auto d_db = -delta.y / gain_scale_.getHalfHeight() * gain_scale_.getMaxDB();

        for (const auto& d : drag_starts_) {
            const auto& s = bridge_.band(d.band);
            const auto& bp = bridge_.params(d.band);
            bridge_.setPlain(bp[BandParams::kFreq],
                             static_cast<float>(std::clamp(d.freq * ratio, FreqScale::kMinFreq, max_f)));
            if (s.hasGain()) {
                const auto node_db = std::clamp(d.node_db + d_db, -30.f, 30.f);
                const auto gain = std::clamp(band_ops::nodeDBToGain(s, node_db), -30.f, 30.f);
                bridge_.setPlain(bp[BandParams::kGain], gain);
                if (s.dynamic_on) {
                    bridge_.setPlain(bp[BandParams::kTargetGain], std::clamp(d.target_gain + (gain - d.gain), -30.f, 30.f));
                }
            } else if (s.hasQ()) {
                // gainless shapes: vertical movement sets resonance / width
                bridge_.setPlain(bp[BandParams::kQ], std::clamp(d.q * std::exp2(-delta.y / 60.f), .025f, 25.f));
            }
        }
    }

    void GraphView::dragQ(const float x, const bool left) {
        const auto& b = bridge_.band(drag_band_);
        const auto f = freq_scale_.xToFreq(x);
        const auto half = left ? static_cast<double>(b.freq) / f : f / static_cast<double>(b.freq);
        bridge_.setPlain(bridge_.params(drag_band_)[BandParams::kQ], qFromRatio(half * half));
    }

    void GraphView::dragTarget(const float dy) {
        const auto& b = bridge_.band(drag_band_);
        auto start = b;
        start.target_gain = drag_starts_.front().target_gain;
        const auto start_node = band_ops::getNodeDB(start, true);
        const auto d_db = -dy / gain_scale_.getHalfHeight() * gain_scale_.getMaxDB();
        const auto gain = std::clamp(band_ops::nodeDBToGain(b, start_node + d_db), -30.f, 30.f);
        bridge_.setPlain(bridge_.params(drag_band_)[BandParams::kTargetGain], gain);
    }

    void GraphView::addBandAt(const juce::Point<float> p, const FilterType type) {
        const auto f = std::clamp(freq_scale_.xToFreq(p.x), FreqScale::kMinFreq, maxFreq(bridge_.getSampleRate()));
        BandSnapshot probe;
        probe.type = type;
        const auto node_db = std::clamp(gain_scale_.yToDB(p.y), -30.f, 30.f);
        const auto gain = std::clamp(band_ops::nodeDBToGain(probe, node_db), -30.f, 30.f);
        const auto band = band_ops::add(bridge_, f, std::abs(gain) < .25f ? 0.f : gain, type);
        if (band < kBandNum) {
            selection_.select(band);
            appear_springs_[band].snap(0.f);
        }
    }

    void GraphView::nudge(const float freq_octaves, const float gain_db) {
        const auto bands = getSelectedBands();
        if (bands.empty()) {
            return;
        }
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        const auto max_f = maxFreq(bridge_.getSampleRate());
        for (const auto b : bands) {
            const auto& s = bridge_.band(b);
            const auto& bp = bridge_.params(b);
            if (freq_octaves != 0.f) {
                values.emplace_back(bp[BandParams::kFreq],
                                    static_cast<float>(std::clamp(s.freq * std::exp2(static_cast<double>(freq_octaves)),
                                                                  FreqScale::kMinFreq, max_f)));
            }
            if (gain_db != 0.f && s.hasGain()) {
                values.emplace_back(bp[BandParams::kGain], std::clamp(s.gain + gain_db, -30.f, 30.f));
            }
        }
        if (!values.empty()) {
            bridge_.setNow(values, "Nudge Band");
        }
    }

    void GraphView::selectAdded(const std::vector<size_t>& bands) {
        std::bitset<kBandNum> bits;
        for (const auto b : bands) {
            bits.set(b);
            appear_springs_[b].snap(0.f);
        }
        selection_.setBits(bits);
    }

    void GraphView::cycleSelection(const bool forwards) {
        // order by frequency, the way bands read on screen
        std::vector<size_t> order;
        for (size_t b = 0; b < kBandNum; ++b) {
            if (bridge_.band(b).isActive()) {
                order.push_back(b);
            }
        }
        if (order.empty()) {
            return;
        }
        std::ranges::sort(order, [this](const size_t a, const size_t b) {
            return bridge_.band(a).freq < bridge_.band(b).freq;
        });
        const auto it = std::ranges::find(order, selection_.getPrimary());
        if (it == order.end()) {
            selection_.select(forwards ? order.front() : order.back());
            return;
        }
        const auto idx = static_cast<size_t>(std::distance(order.begin(), it));
        const auto next = forwards ? (idx + 1) % order.size() : (idx + order.size() - 1) % order.size();
        selection_.select(order[next]);
    }

    void GraphView::setSolo(const size_t band, const bool side_chain) {
        if (band == solo_band_ && side_chain == solo_side_) {
            return;
        }
        solo_band_ = band;
        solo_side_ = band < kBandNum && side_chain;
        bridge_.getController().setSoloWholeIdx(band < kBandNum ? (solo_side_ ? kBandNum + band : band) : kSoloOff);
        repaint();
    }

    void GraphView::undo() {
        if (bridge_.isEditing()) {
            bridge_.endEdit();
        }
        bridge_.getUndoManager().undo();
    }

    void GraphView::redo() {
        if (bridge_.isEditing()) {
            bridge_.endEdit();
        }
        bridge_.getUndoManager().redo();
    }

    void GraphView::showBandMenu(const size_t band) {
        const auto& b = bridge_.band(band);
        juce::PopupMenu shapes, slopes, stereo, menu;
        for (int t = 0; t < zlp::PFilterType::kChoices.size(); ++t) {
            shapes.addItem(100 + t, band_ops::typeName(static_cast<FilterType>(t)), true, static_cast<int>(b.type) == t);
        }
        for (int o = 0; o < zlp::POrder::kChoices.size(); ++o) {
            slopes.addItem(200 + o, zlp::POrder::kChoices[o], b.hasSlope(), static_cast<int>(b.order_idx) == o);
        }
        for (int lr = 0; lr < static_cast<int>(ResponseModel::kNumStereo); ++lr) {
            stereo.addItem(300 + lr, kStereoNames[static_cast<size_t>(lr)], true, static_cast<int>(b.lr) == lr);
        }
        menu.addSectionHeader("Band " + juce::String(band + 1));
        menu.addSubMenu("Shape", shapes);
        menu.addSubMenu("Slope", slopes);
        menu.addSubMenu("Stereo Placement", stereo);
        menu.addSeparator();
        menu.addItem(400, "Dynamic EQ", true, b.dynamic_on);
        menu.addItem(401, "Bypass", true, b.status == zlp::kBypass);
        menu.addItem(403, "Reset Gain", b.hasGain() && std::abs(b.gain) > 1e-3f);
        menu.addSeparator();
        menu.addItem(404, "Copy", true, false);
        menu.addItem(405, "Duplicate", bridge_.findOffBand() < kBandNum, false);
        menu.addSeparator();
        const auto count = selection_.isSelected(band) ? getSelectedBands().size() : 1;
        menu.addItem(402, count > 1 ? "Delete " + juce::String(count) + " Bands" : "Delete Band");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                               localAreaToGlobal(juce::Rectangle<int>(1, 1).withPosition(mouse_pos_.toInt()))),
                           [safe = juce::Component::SafePointer(this), band, count](const int r) {
                               if (safe == nullptr || r == 0) {
                                   return;
                               }
                               auto& self = *safe;
                               if (r >= 100 && r < 200) {
                                   band_ops::setType(self.bridge_, band, static_cast<FilterType>(r - 100));
                               } else if (r >= 200 && r < 300) {
                                   band_ops::setOrder(self.bridge_, band, static_cast<size_t>(r - 200));
                               } else if (r >= 300 && r < 400) {
                                   band_ops::setStereo(self.bridge_, band, static_cast<zlp::FilterStereo>(r - 300));
                               } else if (r == 400) {
                                   band_ops::setDynamic(self.bridge_, band, !self.bridge_.band(band).dynamic_on);
                               } else if (r == 401) {
                                   band_ops::toggleBypass(self.bridge_, {band});
                               } else if (r == 402) {
                                   band_ops::remove(self.bridge_, count > 1 ? self.getSelectedBands()
                                                                            : std::vector<size_t>{band});
                               } else if (r == 404) {
                                   session_ops::copyBands(self.bridge_, count > 1 ? self.getSelectedBands()
                                                                                 : std::vector<size_t>{band});
                               } else if (r == 405) {
                                   const auto added = session_ops::duplicateBands(
                                       self.bridge_, count > 1 ? self.getSelectedBands() : std::vector<size_t>{band});
                                   self.selectAdded(added);
                               } else if (r == 403) {
                                   self.bridge_.setNow({{self.bridge_.params(band)[BandParams::kGain], 0.f}},
                                                       "Reset Gain");
                               }
                           });
    }

    void GraphView::showAddMenu(const juce::Point<float> p) {
        juce::PopupMenu menu;
        const auto f = freq_scale_.xToFreq(p.x);
        menu.addSectionHeader("Add at " + format::freq(f));
        for (const auto t : {FilterType::kPeak, FilterType::kLowShelf, FilterType::kHighShelf, FilterType::kHighPass,
                             FilterType::kLowPass, FilterType::kNotch, FilterType::kBandPass, FilterType::kTiltShelf}) {
            menu.addItem(500 + static_cast<int>(t), band_ops::typeName(t), bridge_.findOffBand() < kBandNum);
        }
        menu.addSeparator();
        menu.addItem(600, "Paste", session_ops::canPaste() && bridge_.findOffBand() < kBandNum);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                               localAreaToGlobal(juce::Rectangle<int>(1, 1).withPosition(p.toInt()))),
                           [safe = juce::Component::SafePointer(this), p](const int r) {
                               if (safe == nullptr) {
                                   return;
                               }
                               if (r == 600) {
                                   safe->selectAdded(session_ops::pasteBands(safe->bridge_));
                               } else if (r >= 500) {
                                   safe->addBandAt(p, static_cast<FilterType>(r - 500));
                               }
                           });
    }

    // ---------------------------------------------------------------- mouse

    void GraphView::mouseMove(const juce::MouseEvent& e) {
        mouse_pos_ = e.position;
        mouse_inside_ = true;
        updateHover(e.position);
        repaint();
    }

    void GraphView::mouseEnter(const juce::MouseEvent& e) {
        mouse_inside_ = true;
        mouse_pos_ = e.position;
        updateHover(e.position);
    }

    void GraphView::mouseExit(const juce::MouseEvent&) {
        mouse_inside_ = false;
        updateHover(mouse_pos_);
        repaint();
    }

    void GraphView::mouseDown(const juce::MouseEvent& e) {
        grabKeyboardFocus();
        mouse_pos_ = e.position;
        if (isMatching()) {
            match_drawing_ = true;
            match_erasing_ = e.mods.isPopupMenu() || e.mods.isAltDown();
            matchDraw(e.position, true);
            return;
        }
        const auto hit = hitTestNodes(e.position);
        drag_start_pos_ = e.position;
        drag_last_pos_ = e.position;
        drag_virtual_pos_ = e.position;
        axis_lock_ = 0;

        if (e.mods.isPopupMenu()) {
            if (hit.type != HitType::kNone) {
                if (!selection_.isSelected(hit.band)) {
                    selection_.select(hit.band);
                }
                showBandMenu(hit.band);
            } else {
                showAddMenu(e.position);
            }
            return;
        }

        switch (hit.type) {
        case HitType::kNode: {
            if (e.mods.isAltDown()) {
                const auto bands = selection_.isSelected(hit.band) ? getSelectedBands() : std::vector<size_t>{hit.band};
                band_ops::remove(bridge_, bands);
                return;
            }
            if (e.mods.isShiftDown() || e.mods.isCommandDown()) {
                selection_.toggle(hit.band);
                if (!selection_.isSelected(hit.band)) {
                    return;
                }
            } else if (!selection_.isSelected(hit.band)) {
                selection_.select(hit.band);
            } else {
                selection_.add(hit.band);
            }
            beginNodeDrag(hit.band);
            drag_mode_ = DragMode::kNode;
            e.source.enableUnboundedMouseMovement(false);
            break;
        }
        case HitType::kQLeft:
        case HitType::kQRight: {
            drag_band_ = hit.band;
            const auto& bp = bridge_.params(hit.band);
            juce::RangedAudioParameter* paras[] = {bp[BandParams::kQ]};
            bridge_.beginEdit(paras, "Change Q");
            drag_mode_ = hit.type == HitType::kQLeft ? DragMode::kQLeft : DragMode::kQRight;
            break;
        }
        case HitType::kTarget: {
            drag_band_ = hit.band;
            const auto& s = bridge_.band(hit.band);
            drag_starts_ = {{hit.band, s.freq, band_ops::getNodeDB(s), s.q, s.target_gain, s.gain}};
            const auto& bp = bridge_.params(hit.band);
            juce::RangedAudioParameter* paras[] = {bp[BandParams::kTargetGain]};
            bridge_.beginEdit(paras, "Change Dynamic Range");
            drag_mode_ = DragMode::kTarget;
            break;
        }
        case HitType::kSide:
        case HitType::kSideLeft:
        case HitType::kSideRight: {
            drag_band_ = hit.band;
            const auto& bp = bridge_.params(hit.band);
            juce::RangedAudioParameter* paras[] = {bp[BandParams::kSideFreq], bp[BandParams::kSideQ],
                                                   bp[BandParams::kSideLink]};
            bridge_.beginEdit(paras, "Change Detector");
            drag_mode_ = hit.type == HitType::kSide ? DragMode::kSide
                                                    : (hit.type == HitType::kSideLeft ? DragMode::kSideLeft
                                                                                       : DragMode::kSideRight);
            if (hit.type == HitType::kSide) {
                // drag relative to the handle, not the click point
                drag_virtual_pos_.x = getSideRange(hit.band).centre;
                drag_start_pos_ = drag_virtual_pos_;
            }
            break;
        }
        case HitType::kPeak: {
            if (!grab_peak_.has_value()) {
                break;
            }
            const auto peak = *grab_peak_;
            const auto band = band_ops::add(bridge_, peak.freq, 0.f, FilterType::kPeak);
            if (band >= kBandNum) {
                break;
            }
            // pull the new band into the snapshot now, so the drag starts from it
            bridge_.refresh();
            selection_.select(band);
            appear_springs_[band].snap(0.f);
            grab_peak_.reset();
            beginNodeDrag(band);
            drag_mode_ = DragMode::kNode;
            // the drag is relative to the new node, which sits on the 0 dB line
            drag_start_pos_ = {peak.x, gain_scale_.getCentreY()};
            drag_virtual_pos_ = {peak.x, gain_scale_.getCentreY()};
            break;
        }
        case HitType::kNone:
            drag_mode_ = DragMode::kPending;
            lasso_initial_ = e.mods.isShiftDown() || e.mods.isCommandDown() ? selection_.getBits()
                                                                            : std::bitset<kBandNum>{};
            break;
        }
        repaint();
    }

    void GraphView::mouseDrag(const juce::MouseEvent& e) {
        mouse_pos_ = e.position;
        if (match_drawing_) {
            if (isMatching()) {
                matchDraw(e.position, false);
            }
            return;
        }
        const auto fine = e.mods.isShiftDown() ? .2f : 1.f;
        drag_virtual_pos_ += (e.position - drag_last_pos_) * fine;
        drag_last_pos_ = e.position;

        switch (drag_mode_) {
        case DragMode::kNode:
            dragNodes(drag_virtual_pos_ - drag_start_pos_, e.mods.isCommandDown());
            break;
        case DragMode::kQLeft:
        case DragMode::kQRight:
            dragQ(drag_virtual_pos_.x, drag_mode_ == DragMode::kQLeft);
            break;
        case DragMode::kTarget:
            dragTarget(drag_virtual_pos_.y - drag_start_pos_.y);
            break;
        case DragMode::kSide:
        case DragMode::kSideLeft:
        case DragMode::kSideRight:
            dragSide(drag_virtual_pos_.x, drag_mode_);
            break;
        case DragMode::kPending:
            if (e.getDistanceFromDragStart() > 3) {
                drag_mode_ = DragMode::kLasso;
            } else {
                break;
            }
            [[fallthrough]];
        case DragMode::kLasso: {
            lasso_rect_ = juce::Rectangle<float>(drag_start_pos_, e.position);
            auto bits = lasso_initial_;
            for (size_t b = 0; b < kBandNum; ++b) {
                if (bridge_.band(b).isActive() && lasso_rect_.contains(getNodePosition(b))) {
                    bits.set(b);
                }
            }
            selection_.setBits(bits);
            repaint();
            break;
        }
        case DragMode::kNone:
            break;
        }
    }

    void GraphView::mouseUp(const juce::MouseEvent& e) {
        if (match_drawing_) {
            match_drawing_ = false;
            return;
        }
        const auto mode = drag_mode_;
        drag_mode_ = DragMode::kNone;
        switch (mode) {
        case DragMode::kNode:
        case DragMode::kQLeft:
        case DragMode::kQRight:
        case DragMode::kTarget:
        case DragMode::kSide:
        case DragMode::kSideLeft:
        case DragMode::kSideRight:
            bridge_.endEdit();
            break;
        case DragMode::kPending:
            if (!e.mods.isShiftDown() && !e.mods.isCommandDown()) {
                selection_.clear();
            }
            break;
        case DragMode::kLasso:
        case DragMode::kNone:
            break;
        }
        axis_lock_ = 0;
        updateHover(e.position);
        repaint();
    }

    void GraphView::mouseDoubleClick(const juce::MouseEvent& e) {
        if (e.mods.isPopupMenu()) {
            return;
        }
        if (isMatching()) {
            match_->clearDrawing();
            return;
        }
        const auto hit = hitTestNodes(e.position);
        if (hit.type == HitType::kNode) {
            band_ops::toggleBypass(bridge_, {hit.band});
        } else if (hit.type == HitType::kSide || hit.type == HitType::kSideLeft || hit.type == HitType::kSideRight) {
            // re-attach the detector to the band
            bridge_.setNow({{bridge_.params(hit.band)[BandParams::kSideLink], 1.f}}, "Link Detector");
        } else if (hit.type == HitType::kNone) {
            const auto f = freq_scale_.xToFreq(e.position.x);
            addBandAt(e.position, band_ops::inferType(f, e.mods.isAltDown()));
        }
    }

    void GraphView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
        if (isMatching()) {
            return;
        }
        const auto hit = hitTestNodes(e.position);
        auto band = hit.type != HitType::kNone ? hit.band : kBandNum;
        if (band >= kBandNum) {
            return;
        }
        const auto delta = (std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX)
            * (wheel.isReversed ? -1.f : 1.f);
        if (std::abs(delta) < 1e-6f) {
            return;
        }
        const auto& s = bridge_.band(band);
        const auto& bp = bridge_.params(band);
        const auto fine = (e.mods.isShiftDown() ? .2f : 1.f) * InteractionSettings::wheel_sensitivity;
        if (!wheel_editing_) {
            juce::RangedAudioParameter* paras[] = {bp[BandParams::kQ], bp[BandParams::kGain]};
            bridge_.beginEdit(paras, e.mods.isAltDown() ? "Change Gain" : "Change Q");
            wheel_editing_ = true;
            drag_band_ = band;
        }
        if (e.mods.isAltDown() && s.hasGain()) {
            bridge_.setPlain(bp[BandParams::kGain], std::clamp(s.gain + delta * 6.f * fine, -30.f, 30.f));
        } else if (s.hasQ()) {
            bridge_.setPlain(bp[BandParams::kQ], std::clamp(s.q * std::exp2(delta * 1.5f * fine), .025f, 25.f));
        }
        readout_band_ = band;
        readout_delay_ = motion::kReadoutDelay;
        startTimer(450);
    }

    void GraphView::timerCallback() {
        stopTimer();
        if (wheel_editing_) {
            wheel_editing_ = false;
            bridge_.endEdit();
        }
    }

    bool GraphView::keyPressed(const juce::KeyPress& key) {
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();
        if (isMatching()) {
            if (code == juce::KeyPress::escapeKey) {
                match_->cancel();
                return true;
            }
            return false;
        }
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
            if (const auto bands = getSelectedBands(); !bands.empty()) {
                band_ops::remove(bridge_, bands);
            }
            return true;
        }
        if (code == juce::KeyPress::escapeKey) {
            selection_.clear();
            return true;
        }
        if (code == juce::KeyPress::tabKey) {
            cycleSelection(!mods.isShiftDown());
            return true;
        }
        if (mods.isCommandDown() && (code == 'Z' || code == 'z')) {
            mods.isShiftDown() ? redo() : undo();
            return true;
        }
        if (mods.isCommandDown() && (code == 'C' || code == 'c')) {
            session_ops::copyBands(bridge_, getSelectedBands());
            return true;
        }
        if (mods.isCommandDown() && (code == 'V' || code == 'v')) {
            if (const auto added = session_ops::pasteBands(bridge_); !added.empty()) {
                selectAdded(added);
            }
            return true;
        }
        if (mods.isCommandDown() && (code == 'D' || code == 'd')) {
            if (const auto added = session_ops::duplicateBands(bridge_, getSelectedBands()); !added.empty()) {
                selectAdded(added);
            }
            return true;
        }
        if (mods.isCommandDown() && (code == 'A' || code == 'a')) {
            std::bitset<kBandNum> bits;
            for (size_t b = 0; b < kBandNum; ++b) {
                bits[b] = bridge_.band(b).isActive();
            }
            selection_.setBits(bits);
            return true;
        }
        const auto fine = mods.isShiftDown();
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
            const auto step = fine ? 1.f / 96.f : 1.f / 24.f;
            nudge(code == juce::KeyPress::leftKey ? -step : step, 0.f);
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
            const auto step = fine ? .1f : .5f;
            nudge(0.f, code == juce::KeyPress::downKey ? -step : step);
            return true;
        }
        if (!mods.isAnyModifierKeyDown() && (code == 'S' || code == 's')) {
            const auto side = hover_hit_.type == HitType::kSide || hover_hit_.type == HitType::kSideLeft
                || hover_hit_.type == HitType::kSideRight;
            const auto band = side ? hover_hit_.band
                                   : (selection_.getHovered() < kBandNum ? selection_.getHovered() : selection_.getPrimary());
            if (band < kBandNum) {
                key_solo_ = true;
                setSolo(band, side);
            }
            return true;
        }
        if (!mods.isAnyModifierKeyDown() && (code == 'B' || code == 'b')) {
            if (const auto bands = getSelectedBands(); !bands.empty()) {
                band_ops::toggleBypass(bridge_, bands);
            }
            return true;
        }
        return false;
    }

    bool GraphView::keyStateChanged(const bool is_key_down) {
        if (!is_key_down && key_solo_ && !juce::KeyPress::isKeyCurrentlyDown('S')
            && !juce::KeyPress::isKeyCurrentlyDown('s')) {
            key_solo_ = false;
            setSolo(kBandNum);
            return true;
        }
        return false;
    }
}
