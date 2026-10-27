// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "history_view.hpp"

#include <altvane_ui/theme/fonts.hpp>
#include <altvane_ui/util/value_format.hpp>

#include "theme/deesser_tokens.hpp"

namespace altvane::deesser {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kTopDb = 0.f;
        constexpr float kBottomDb = -66.f;
        // gain reduction hangs from the top: its full scale reaches this far down
        constexpr float kReductionScaleDb = 24.f;
        constexpr float kReductionDepth = .42f;
        constexpr std::array<double, 3> kSpans{2.0, 4.0, 8.0};

        struct Column {
            float x, input, output, band, reduction, sibilance;
        };
    }

    HistoryView::HistoryView(PluginProcessor& p, ui::ParamEditor& editor) :
        p_ref_(p), editor_(editor), frames_(kCapacity) {
        setTitle("Level history");
        setDescription("The last seconds of audio: the signal, its sibilance band, the gain reduction and the threshold");
        static constexpr std::array<const char*, 3> kTips{"Show the last 2 seconds", "Show the last 4 seconds",
                                                          "Show the last 8 seconds"};
        for (size_t i = 0; i < spans_.size(); ++i) {
            spans_[i].setTooltip(kTips[i]);
            spans_[i].on_click = [this, i] { setSeconds(kSpans[i]); };
            addAndMakeVisible(spans_[i]);
        }
        freeze_.setToggleable(true);
        freeze_.setTooltip("Freeze the history (F)");
        freeze_.setTitle("Freeze");
        freeze_.on_click = [this] { setFrozen(!frozen_); };
        addAndMakeVisible(freeze_);
        setSeconds(4.0);
    }

    // ---------------------------------------------------------------- geometry

    float HistoryView::dbToY(const float db) const {
        const auto t = std::clamp((kTopDb - db) / (kTopDb - kBottomDb), 0.f, 1.f);
        return plot_.getY() + t * plot_.getHeight();
    }

    float HistoryView::yToDb(const float y) const {
        return kTopDb - (y - plot_.getY()) / plot_.getHeight() * (kTopDb - kBottomDb);
    }

    float HistoryView::reductionY(const float db) const {
        return plot_.getY() + plot_.getHeight() * kReductionDepth * std::clamp(db / kReductionScaleDb, 0.f, 1.f);
    }

    juce::RangedAudioParameter* HistoryView::thresholdParam() const {
        return p_ref_.parameters_.getParameter(params::kThreshold);
    }

    bool HistoryView::nearThreshold(const juce::Point<float> p) const {
        return plot_.expanded(0.f, 6.f).contains(p) && std::abs(p.y - getThresholdY()) <= 6.f;
    }

    int HistoryView::getHoveredParameterIndex() const {
        return hover_threshold_ || dragging_ ? thresholdParam()->getParameterIndex() : -1;
    }

    // ---------------------------------------------------------------- state

    void HistoryView::setSeconds(const double seconds) {
        seconds_ = seconds;
        for (size_t i = 0; i < spans_.size(); ++i) {
            spans_[i].setOn(std::abs(kSpans[i] - seconds) < .01);
        }
        rebuildGrid();
        repaint();
    }

    void HistoryView::setFrozen(const bool frozen) {
        frozen_ = frozen;
        freeze_.setOn(frozen);
        repaint();
    }

    void HistoryView::tick(const double dt) {
        bool dirty = false;
        dsp::HistoryFrame frame, latest;
        bool pulled = false;
        while (p_ref_.getEngine().pullHistory(frame)) {
            pulled = true;
            latest = frame;
            if (!frozen_) {
                frames_[count_ % kCapacity] = frame;
                ++count_;
            }
        }
        dirty = pulled && !frozen_;
        quiet_seconds_ = pulled && latest.input_db > -90.f ? 0.0 : quiet_seconds_ + dt;

        const auto threshold = p_ref_.parameters_.getRawParameterValue(params::kThreshold)->load();
        const auto range = p_ref_.parameters_.getRawParameterValue(params::kRange)->load();
        if (threshold != threshold_db_ || range != range_db_) {
            threshold_db_ = threshold;
            range_db_ = range;
            dirty = true;
        }
        if (pulled) {
            crossing_.setTarget(latest.reduction_db > .5f);
        } else if (quiet_seconds_ > .1) {
            crossing_.setTarget(false);
        }
        dirty = crossing_.update(static_cast<float>(dt)) || dirty;
        if (dirty) {
            repaint();
        }
    }

    // ---------------------------------------------------------------- painting

    void HistoryView::resized() {
        plot_ = getLocalBounds().toFloat().withTrimmedLeft(40.f).withTrimmedRight(34.f).withTrimmedTop(12.f).withTrimmedBottom(20.f);
        auto controls = juce::Rectangle<float>(plot_.getRight() - 8.f - 3.f * 30.f - 4.f - 24.f, plot_.getY() + 8.f, 0.f, 20.f);
        for (auto& chip : spans_) {
            chip.setBounds(controls.withWidth(30.f).toNearestInt());
            controls.translate(31.f, 0.f);
        }
        freeze_.setBounds(controls.withX(controls.getX() + 3.f).withWidth(24.f).toNearestInt());
        rebuildGrid();
    }

    void HistoryView::rebuildGrid() {
        if (getWidth() <= 0 || getHeight() <= 0) {
            return;
        }
        const auto scale = juce::Component::getApproximateScaleFactorForComponent(this);
        grid_ = juce::Image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(getWidth()) * scale),
                            juce::roundToInt(static_cast<float>(getHeight()) * scale), true);
        juce::Graphics g(grid_);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setFont(Fonts::get().font(type::kCaption));
        // level, every 12 dB
        for (float db = 0.f; db > kBottomDb; db -= 12.f) {
            const auto y = dbToY(db);
            g.setColour(db == 0.f ? colour::kGridMajor : colour::kGridMinor);
            g.fillRect(juce::Rectangle<float>(plot_.getX(), std::round(y), plot_.getWidth(), 1.f));
            g.setColour(colour::kTextTertiary);
            const auto label = db == 0.f ? juce::String("0") : ui::format::kMinus + juce::String(juce::roundToInt(-db));
            g.drawText(label, juce::Rectangle<float>(0.f, y - 7.f, plot_.getX() - 8.f, 14.f), juce::Justification::centredRight);
        }
        // gain reduction, on the right
        g.setColour(colour::kReduction.withAlpha(.6f));
        for (float db = 6.f; db <= kReductionScaleDb; db += 6.f) {
            const auto y = reductionY(db);
            g.fillRect(juce::Rectangle<float>(plot_.getRight(), y, 4.f, 1.f));
            g.drawText(juce::String(juce::roundToInt(db)), juce::Rectangle<float>(plot_.getRight() + 6.f, y - 7.f, 26.f, 14.f),
                       juce::Justification::centredLeft);
        }
        // time, from now (the right edge) back
        const auto step = seconds_ > 4.5 ? 2.0 : seconds_ > 2.5 ? 1.0 : .5;
        for (double s = step; s < seconds_ - .01; s += step) {
            const auto x = plot_.getRight() - static_cast<float>(s / seconds_) * plot_.getWidth();
            g.setColour(colour::kGridMinor);
            g.fillRect(juce::Rectangle<float>(std::round(x), plot_.getY(), 1.f, plot_.getHeight()));
            g.setColour(colour::kTextTertiary);
            g.drawText(ui::format::kMinus + juce::String(s, s < 1.0 ? 1 : 0) + " s",
                       juce::Rectangle<float>(x - 30.f, plot_.getBottom() + 4.f, 60.f, 14.f), juce::Justification::centred);
        }
    }

    void HistoryView::paint(juce::Graphics& g) {
        g.fillAll(colour::kGraph);
        g.drawImage(grid_, getLocalBounds().toFloat());
        const auto accent = colour::kAccent;

        // columns: frames grouped on a fixed grid, so the picture does not shimmer as it scrolls
        const auto visible = seconds_ * dsp::kHistoryRate;
        const auto frames_per_px = visible / static_cast<double>(plot_.getWidth());
        const auto group = static_cast<uint64_t>(std::max(1.0, std::floor(frames_per_px)));
        const auto available = std::min<uint64_t>(count_, kCapacity);
        const auto first = count_ - available;
        const auto span = static_cast<uint64_t>(visible) + 2 * group;
        const auto start = std::max(first, count_ > span ? count_ - span : 0) / group * group;
        std::vector<Column> columns;
        columns.reserve(static_cast<size_t>(visible / static_cast<double>(group)) + 4);
        for (auto g0 = start; g0 < count_; g0 += group) {
            Column c{0.f, -120.f, -120.f, -120.f, 0.f, 0.f};
            const auto end = std::min(g0 + group, count_);
            for (auto i = std::max(g0, first); i < end; ++i) {
                const auto& f = frames_[i % kCapacity];
                c.input = std::max(c.input, f.input_db);
                c.output = std::max(c.output, f.output_db);
                c.band = std::max(c.band, f.band_db);
                c.reduction = std::max(c.reduction, f.reduction_db);
                c.sibilance = std::max(c.sibilance, f.sibilance);
            }
            c.x = plot_.getRight() - static_cast<float>(static_cast<double>(count_ - end) / frames_per_px);
            columns.push_back(c);
        }

        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(plot_.toNearestInt());
            const auto bottom = plot_.getBottom() + 1.f;
            if (columns.size() > 1) {
                auto area = [&](auto value) {
                    juce::Path p;
                    p.startNewSubPath(columns.front().x, bottom);
                    for (const auto& c : columns) {
                        p.lineTo(c.x, dbToY(value(c)));
                    }
                    p.lineTo(columns.back().x, bottom);
                    p.closeSubPath();
                    return p;
                };
                auto line = [&](auto value) {
                    juce::Path p;
                    p.startNewSubPath(columns.front().x, dbToY(value(columns.front())));
                    for (const auto& c : columns) {
                        p.lineTo(c.x, dbToY(value(c)));
                    }
                    return p;
                };
                const auto top = plot_.getY();

                // the full signal: graphite
                g.setGradientFill(juce::ColourGradient(colour::kLevel.withAlpha(.16f), 0.f, top,
                                                       colour::kLevel.withAlpha(.04f), 0.f, bottom, false));
                g.fillPath(area([](const Column& c) { return c.input; }));
                g.setColour(colour::kLevel.withAlpha(.32f));
                g.strokePath(line([](const Column& c) { return c.input; }), juce::PathStrokeType(1.f));

                // the detection band, after reduction
                const auto after = [](const Column& c) { return c.band - c.reduction; };
                g.setGradientFill(juce::ColourGradient(colour::kBand.withAlpha(.30f), 0.f, top + plot_.getHeight() * .3f,
                                                       colour::kBand.withAlpha(.05f), 0.f, bottom, false));
                g.fillPath(area(after));

                // what was taken: bitten out of the band, in the sibilance colour
                juce::Path taken;
                taken.startNewSubPath(columns.front().x, dbToY(columns.front().band));
                for (const auto& c : columns) {
                    taken.lineTo(c.x, dbToY(c.band));
                }
                for (auto it = columns.rbegin(); it != columns.rend(); ++it) {
                    taken.lineTo(it->x, dbToY(after(*it)));
                }
                taken.closeSubPath();
                g.setColour(colour::kSibilance.withAlpha(.78f));
                g.fillPath(taken);
                g.setColour(colour::kBand.withAlpha(.6f));
                g.strokePath(line(after), juce::PathStrokeType(1.1f));

                // where the detector heard sibilance, along the floor
                juce::RectangleList<float> ticks;
                for (size_t i = 1; i < columns.size(); ++i) {
                    if (columns[i].sibilance > .5f) {
                        ticks.addWithoutMerging({columns[i - 1].x, bottom - 4.f, columns[i].x - columns[i - 1].x + .5f, 3.f});
                    }
                }
                g.setColour(colour::kSibilance.withAlpha(.55f));
                g.fillRectList(ticks);

                // gain reduction, hanging from the top
                juce::Path reduction;
                reduction.startNewSubPath(columns.front().x, top - 1.f);
                for (const auto& c : columns) {
                    reduction.lineTo(c.x, reductionY(c.reduction));
                }
                reduction.lineTo(columns.back().x, top - 1.f);
                reduction.closeSubPath();
                g.setGradientFill(juce::ColourGradient(colour::kReduction.withAlpha(.22f), 0.f, top,
                                                       colour::kReduction.withAlpha(.03f), 0.f, reductionY(range_db_ + 3.f), false));
                g.fillPath(reduction);
                juce::Path reduction_line;
                reduction_line.startNewSubPath(columns.front().x, reductionY(columns.front().reduction));
                for (const auto& c : columns) {
                    reduction_line.lineTo(c.x, reductionY(c.reduction));
                }
                g.setColour(colour::kReduction.withAlpha(.85f));
                g.strokePath(reduction_line, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // the threshold
            const auto y = getThresholdY();
            const auto glow = crossing_.getEased();
            const auto active = hover_threshold_ || dragging_;
            if (glow > 0.f || active) {
                g.setColour(accent.withAlpha(.10f + .12f * std::max(glow, active ? 1.f : 0.f)));
                g.fillRect(juce::Rectangle<float>(plot_.getX(), y - 3.f, plot_.getWidth(), 6.f));
            }
            g.setColour(accent.withAlpha(active ? 1.f : .85f));
            g.fillRect(juce::Rectangle<float>(plot_.getX(), y - .75f, plot_.getWidth(), 1.5f));
        }

        // the threshold's tag, at the left where nothing scrolls in yet
        {
            const auto y = getThresholdY();
            const auto text = params::formatDb(threshold_db_);
            const auto w = Fonts::get().getTabularWidth(text, type::kSmall, Fonts::kSemiBold) + 14.f;
            const auto tag = juce::Rectangle<float>(plot_.getX() + 6.f, y - 10.f, w, 20.f);
            g.setColour(colour::kOverlay);
            g.fillRoundedRectangle(tag, size::kRadiusSmall);
            g.setColour(accent.withAlpha(hover_threshold_ || dragging_ ? .9f : .5f));
            g.drawRoundedRectangle(tag.reduced(.5f), size::kRadiusSmall, 1.f);
            g.setColour(colour::kTextPrimary);
            Fonts::get().drawTabular(g, text, tag, juce::Justification::centred, type::kSmall, Fonts::kSemiBold);
        }

        if (hover_x_ && !dragging_ && !columns.empty()) {
            paintReadout(g, *hover_x_, frames_per_px);
        }
        if (count_ == 0) {
            g.setFont(Fonts::get().font(type::kBody));
            g.setColour(colour::kTextTertiary);
            g.drawText("Play audio: esses light up as they are taken down",
                       plot_.withTrimmedBottom(plot_.getHeight() * .4f), juce::Justification::centred);
        }
        if (frozen_) {
            g.setFont(captionFont());
            g.setColour(colour::kAccent);
            g.drawText("FROZEN", juce::Rectangle<float>(plot_.getX() + 8.f, plot_.getY() + 8.f, 80.f, 16.f),
                       juce::Justification::centredLeft);
        }
    }

    void HistoryView::paintReadout(juce::Graphics& g, const float x, const double frames_per_pixel) {
        if (!plot_.contains(x, plot_.getCentreY())) {
            return;
        }
        const auto back = static_cast<double>(plot_.getRight() - x) * frames_per_pixel;
        if (back >= static_cast<double>(std::min<uint64_t>(count_, kCapacity))) {
            return;
        }
        const auto index = count_ - 1 - static_cast<uint64_t>(back);
        // the loudest moment within a few pixels, so a thin ess is easy to point at
        dsp::HistoryFrame f;
        f.band_db = -200.f;
        const auto reach = static_cast<uint64_t>(std::max(1.0, frames_per_pixel * 3.0));
        for (uint64_t i = index > reach ? index - reach : 0; i <= std::min(index + reach, count_ - 1); ++i) {
            if (i + kCapacity < count_) {
                continue;
            }
            if (const auto& candidate = frames_[i % kCapacity]; candidate.band_db > f.band_db) {
                f = candidate;
            }
        }
        g.setColour(colour::kTextPrimary.withAlpha(.25f));
        g.fillRect(juce::Rectangle<float>(x, plot_.getY(), 1.f, plot_.getHeight()));

        const std::array<std::pair<juce::String, juce::String>, 3> rows{
            std::pair{juce::String("BAND"), params::formatDb(f.band_db)},
            std::pair{juce::String("GR"), f.reduction_db > .05f ? params::formatDb(-f.reduction_db) : juce::String("0.0 dB")},
            std::pair{juce::String("TIME"), ui::format::kMinus + juce::String(back / dsp::kHistoryRate, 2) + " s"},
        };
        const auto card_w = 118.f, card_h = 14.f * 3.f + 12.f;
        auto card = juce::Rectangle<float>(x + 10.f, plot_.getY() + 34.f, card_w, card_h);
        if (card.getRight() > plot_.getRight() - 4.f) {
            card.setX(x - 10.f - card_w);
        }
        g.setColour(colour::kOverlay);
        g.fillRoundedRectangle(card, size::kRadius);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(card.reduced(.5f), size::kRadius, 1.f);
        auto rows_area = card.reduced(10.f, 6.f);
        for (size_t r = 0; r < rows.size(); ++r) {
            auto row = rows_area.removeFromTop(14.f);
            g.setFont(captionFont());
            g.setColour(r == 1 && f.reduction_db > .05f ? colour::kReduction : colour::kTextTertiary);
            g.drawText(rows[r].first, row, juce::Justification::centredLeft);
            g.setColour(colour::kTextPrimary);
            Fonts::get().drawTabular(g, rows[r].second, row, juce::Justification::centredRight, type::kSmall);
        }
    }

    // ---------------------------------------------------------------- mouse

    void HistoryView::updateTooltip() {
        setTooltip(hover_threshold_ ? "Threshold: drag to set (shift for fine), double-click to reset"
                                    : "Click to freeze the history");
    }

    void HistoryView::mouseMove(const juce::MouseEvent& e) {
        const auto near = nearThreshold(e.position);
        if (near != hover_threshold_) {
            hover_threshold_ = near;
            setMouseCursor(near ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
            updateTooltip();
        }
        hover_x_ = plot_.contains(e.position) ? std::optional(e.position.x) : std::nullopt;
        repaint();
    }

    void HistoryView::mouseExit(const juce::MouseEvent&) {
        hover_threshold_ = false;
        hover_x_.reset();
        repaint();
    }

    void HistoryView::mouseDown(const juce::MouseEvent& e) {
        if (!nearThreshold(e.position)) {
            return;
        }
        auto* para = thresholdParam();
        if (e.mods.isPopupMenu()) {
            // the host's own parameter menu: automation, MIDI learn, etc.
            auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
            if (editor != nullptr && editor->getHostContext() != nullptr) {
                if (auto menu = editor->getHostContext()->getContextMenuForParameter(para)) {
                    menu->showNativeMenu(editor->getLocalPoint(this, e.getPosition()));
                }
            }
            return;
        }
        if (e.mods.isCommandDown()) {
            editor_.setNow({{para, params::kThresholdDefault}}, "Reset Threshold");
            return;
        }
        dragging_ = true;
        drag_start_db_ = threshold_db_;
        drag_start_y_ = e.position.y;
        juce::RangedAudioParameter* paras[] = {para};
        editor_.beginEdit(paras, "Change Threshold");
        edit_id_ = editor_.getEditId();
        repaint();
    }

    void HistoryView::mouseDrag(const juce::MouseEvent& e) {
        if (!dragging_) {
            return;
        }
        const auto fine = e.mods.isShiftDown() ? .2f : 1.f;
        const auto per_pixel = (kTopDb - kBottomDb) / plot_.getHeight();
        const auto db = std::clamp(drag_start_db_ - (e.position.y - drag_start_y_) * per_pixel * fine,
                                   params::kThresholdMin, params::kThresholdMax);
        editor_.setPlain(thresholdParam(), std::round(db * 10.f) / 10.f);
        threshold_db_ = db;
        repaint();
    }

    void HistoryView::mouseUp(const juce::MouseEvent& e) {
        if (dragging_) {
            if (editor_.isEditing(edit_id_)) {
                editor_.endEdit();
            }
            dragging_ = false;
            repaint();
        } else if (!e.mods.isPopupMenu() && e.mouseWasClicked() && plot_.contains(e.position) && !nearThreshold(e.position)) {
            setFrozen(!frozen_);
        }
    }

    void HistoryView::mouseDoubleClick(const juce::MouseEvent& e) {
        if (nearThreshold(e.position)) {
            editor_.setNow({{thresholdParam(), params::kThresholdDefault}}, "Reset Threshold");
        }
    }
}
