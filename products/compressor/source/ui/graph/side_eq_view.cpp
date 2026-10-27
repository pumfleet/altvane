// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "side_eq_view.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/theme/look_and_feel.hpp"
#include "altvane_ui/util/value_format.hpp"
#include "graph_helpers.hpp"
#include "../model/formats.hpp"
#include "../theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;
    using ui::Fonts;
    using zldsp::filter::FilterType;

    namespace {
        // the spectrum is drawn from −80 dB at the bottom of the plot to 0 dB at the top
        constexpr float kSpectrumFloor = -80.f;
        constexpr std::array kGridFreqs{20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f};

        juce::String freqLabel(const float f) {
            return f >= 1000.f ? juce::String(juce::roundToInt(f / 1000.f)) + "k" : juce::String(juce::roundToInt(f));
        }

        bool hasSlope(const int type) {
            return type == FilterType::kLowPass || type == FilterType::kHighPass;
        }
    }

    bool SideEqView::Band::hasGain() const {
        return type == FilterType::kPeak || type == FilterType::kLowShelf || type == FilterType::kHighShelf
               || type == FilterType::kTiltShelf;
    }

    SideEqView::SideEqView(CompBridge& bridge, SideSpectrum& spectrum) : bridge_(bridge), spectrum_(spectrum) {
        setOpaque(true);
        setTitle("Side-chain EQ");
        setDescription("Double-click to add a band. Drag to move, scroll for Q, Alt-click to bypass, "
            "right-click for shape and delete.");
    }

    SideEqView::~SideEqView() {
        timerCallback();
    }

    juce::Rectangle<float> SideEqView::getPlot() const {
        auto b = getLocalBounds().toFloat();
        b.removeFromLeft(kPadX);
        b.removeFromRight(kPadX + 18.f);
        b.removeFromTop(kPadTop);
        b.removeFromBottom(kPadBottom);
        return b;
    }

    float SideEqView::xOf(const float freq) const {
        const auto p = getPlot();
        return p.getX() + std::log(freq / kMinFreq) / std::log(kMaxFreq / kMinFreq) * p.getWidth();
    }

    float SideEqView::freqOf(const float x) const {
        const auto p = getPlot();
        return kMinFreq * std::pow(kMaxFreq / kMinFreq, (x - p.getX()) / std::max(p.getWidth(), 1.f));
    }

    float SideEqView::yOf(const float db) const {
        const auto p = getPlot();
        return p.getCentreY() - db / max_db_ * p.getHeight() * .5f;
    }

    float SideEqView::dbOf(const float y) const {
        const auto p = getPlot();
        return (p.getCentreY() - y) / std::max(p.getHeight() * .5f, 1.f) * max_db_;
    }

    juce::RangedAudioParameter* SideEqView::para(const char* id, const size_t band) const {
        return bridge_.para(id + juce::String(band));
    }

    bool SideEqView::hasBands() const {
        return std::any_of(bands_.begin(), bands_.end(), [](const Band& b) { return b.isUsed(); });
    }

    void SideEqView::resized() {
        needs_response_ = true;
    }

    void SideEqView::tick() {
        std::array<Band, zlp::kBandNum> bands;
        for (size_t i = 0; i < zlp::kBandNum; ++i) {
            const auto s = juce::String(i);
            auto& b = bands[i];
            b.status = bridge_.getChoice(zlp::PFilterStatus::kID + s);
            b.type = bridge_.getChoice(zlp::PFilterType::kID + s);
            b.order = bridge_.getChoice(zlp::POrder::kID + s);
            b.freq = bridge_.get(zlp::PFreq::kID + s);
            b.gain = bridge_.get(zlp::PGain::kID + s);
            b.q = bridge_.get(zlp::PQ::kID + s);
        }
        const auto max_db = zlstate::PEQMaxDB::kDBs[static_cast<size_t>(std::clamp(
            bridge_.getDisplayChoice(zlstate::PEQMaxDB::kID), 0, 2))];
        const auto sr = bridge_.getProcessor().getAtomicSampleRate();
        if (bands != bands_ || std::abs(max_db - max_db_) > .01f || std::abs(sr - sample_rate_) > .5) {
            bands_ = bands;
            max_db_ = max_db;
            if (std::abs(sr - sample_rate_) > .5) {
                sample_rate_ = sr;
                ideal_.prepare(sr);
            }
            needs_response_ = true;
        }
        eq_on_ = !bridge_.getBool(zlp::PSideEQBypass::kID);
        if (needs_response_) {
            updateResponse();
        }
        buildSpectrumPath();
        repaint();
    }

    void SideEqView::updateResponse() {
        needs_response_ = false;
        const auto plot = getPlot();
        const auto n = static_cast<size_t>(std::max(juce::roundToInt(plot.getWidth()) / 2 + 1, 2));
        ws_.resize(n);
        sum_db_.assign(n, 0.f);
        const auto sr = sample_rate_ > 0.0 ? sample_rate_ : 48000.0;
        for (size_t i = 0; i < n; ++i) {
            const auto x = plot.getX() + static_cast<float>(i) * 2.f;
            ws_[i] = static_cast<float>(std::min(2.0 * juce::MathConstants<double>::pi * freqOf(x) / sr,
                                                 juce::MathConstants<double>::pi));
        }
        for (size_t band = 0; band < zlp::kBandNum; ++band) {
            auto& out = band_db_[band];
            out.assign(n, 0.f);
            const auto& b = bands_[band];
            if (!b.isUsed()) {
                continue;
            }
            const zldsp::filter::FilterParameters paras{
                static_cast<FilterType>(b.type), zlp::POrder::kOrderArray[static_cast<size_t>(b.order)],
                std::min(static_cast<double>(b.freq), .49964 * sr), static_cast<double>(b.gain),
                static_cast<double>(b.q)
            };
            ideal_.forceUpdate(paras);
            ideal_.updateMagnitudeSquare(ws_, out);
            zldsp::vector::sqr_mag_to_db(out.data(), out.size());
            if (b.isOn()) {
                for (size_t i = 0; i < n; ++i) {
                    sum_db_[i] += out[i];
                }
            }
        }
    }

    void SideEqView::buildSpectrumPath() {
        spectrum_path_.clear();
        const auto& spectrum = spectrum_.getSpectrum();
        const auto width = spectrum_.getBinWidth();
        if (spectrum.size() < 4) {
            return;
        }
        const auto plot = getPlot();
        const auto bottom = plot.getBottom();
        const auto yOfLevel = [&](const float db) {
            return std::clamp(bottom - (db - kSpectrumFloor) / -kSpectrumFloor * plot.getHeight(), plot.getY() - 6.f,
                              bottom);
        };
        // the loudest bin per pixel column
        spectrum_path_.startNewSubPath(plot.getX(), bottom);
        auto column = -1;
        auto column_db = -240.f;
        for (size_t i = 1; i < spectrum.size(); ++i) {
            const auto f = static_cast<float>(static_cast<double>(i) * width);
            if (f < kMinFreq) {
                continue;
            }
            if (f > kMaxFreq) {
                break;
            }
            const auto x = static_cast<int>(xOf(f));
            if (x != column && column >= 0) {
                spectrum_path_.lineTo(static_cast<float>(column), yOfLevel(column_db));
                column_db = -240.f;
            }
            column = x;
            column_db = std::max(column_db, spectrum[i]);
        }
        if (column >= 0) {
            spectrum_path_.lineTo(static_cast<float>(column), yOfLevel(column_db));
        }
        spectrum_path_.lineTo(plot.getRight(), bottom);
        spectrum_path_.closeSubPath();
    }

    juce::Point<float> SideEqView::nodePosition(const size_t band) const {
        const auto& b = bands_[band];
        const auto gain = b.hasGain() ? std::clamp(b.gain, -max_db_, max_db_) : 0.f;
        return {xOf(std::clamp(b.freq, kMinFreq, kMaxFreq)), yOf(gain)};
    }

    int SideEqView::bandAt(const juce::Point<float> p) const {
        auto best = -1;
        auto best_d = 12.f;
        for (size_t i = 0; i < zlp::kBandNum; ++i) {
            if (!bands_[i].isUsed()) {
                continue;
            }
            const auto d = p.getDistanceFrom(nodePosition(i));
            if (d < best_d) {
                best_d = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    void SideEqView::paint(juce::Graphics& g) {
        g.fillAll(colour::kGraph);
        const auto plot = getPlot();
        const auto b = getLocalBounds().toFloat();

        // grid
        g.setFont(graph::labelFont());
        for (const auto f : kGridFreqs) {
            const auto x = std::round(xOf(f)) + .5f;
            g.setColour(juce::Colour{0x0DFFFFFF});
            g.fillRect(juce::Rectangle<float>(x - .5f, plot.getY() - 8.f, 1.f, plot.getHeight() + 8.f));
            g.setColour(juce::Colour{0x4DFFFFFF});
            g.drawText(freqLabel(f), juce::Rectangle<float>(x - 20.f, b.getBottom() - 16.f, 40.f, 14.f),
                       juce::Justification::centred);
        }
        const auto step = max_db_ <= 6.f ? 2.f : max_db_ <= 12.f ? 6.f : 10.f;
        for (auto d = -max_db_; d <= max_db_ + .01f; d += step) {
            const auto y = std::round(yOf(d)) + .5f;
            g.setColour(std::abs(d) < .01f ? juce::Colour{0x1FFFFFFF} : juce::Colour{0x0AFFFFFF});
            g.fillRect(juce::Rectangle<float>(plot.getX(), y - .5f, plot.getWidth(), 1.f));
            if (std::abs(d) > .01f) {
                g.setColour(juce::Colour{0x47FFFFFF});
                g.drawText((d > 0.f ? "+" : ui::format::kMinus) + juce::String(juce::roundToInt(std::abs(d))),
                           juce::Rectangle<float>(plot.getRight() + 2.f, y - 7.f, 22.f, 14.f),
                           juce::Justification::centredLeft);
            }
        }

        // what the detector hears
        g.setGradientFill(juce::ColourGradient(colour::kSpectrumSide.withAlpha(.22f), 0.f, plot.getY(),
                                               colour::kSpectrumSide.withAlpha(.03f), 0.f, plot.getBottom(), false));
        g.fillPath(spectrum_path_);
        g.setColour(colour::kSpectrumSide.withAlpha(.55f));
        g.strokePath(spectrum_path_, juce::PathStrokeType(1.f));

        // bands
        const auto n = sum_db_.size();
        const auto clampDB = [&](const float db) { return std::clamp(db, -max_db_ - 2.f, max_db_ + 2.f); };
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(plot.expanded(0.f, 6.f).toNearestInt());
            for (size_t band = 0; band < zlp::kBandNum; ++band) {
                const auto& bd = bands_[band];
                if (!bd.isOn() || band_db_[band].size() != n) {
                    continue;
                }
                juce::Path fill;
                fill.startNewSubPath(plot.getX(), yOf(0.f));
                for (size_t i = 0; i < n; ++i) {
                    fill.lineTo(plot.getX() + static_cast<float>(i) * 2.f, yOf(clampDB(band_db_[band][i])));
                }
                fill.lineTo(plot.getX() + static_cast<float>(n - 1) * 2.f, yOf(0.f));
                fill.closeSubPath();
                const auto active = hover_ == static_cast<int>(band) || drag_ == static_cast<int>(band);
                g.setColour(colour::kBands[band].withAlpha(eq_on_ ? (active ? .2f : .1f) : .04f));
                g.fillPath(fill);
            }
            juce::Path sum;
            for (size_t i = 0; i < n; ++i) {
                const auto x = plot.getX() + static_cast<float>(i) * 2.f;
                const auto y = yOf(clampDB(sum_db_[i]));
                if (i == 0) {
                    sum.startNewSubPath(x, y);
                } else {
                    sum.lineTo(x, y);
                }
            }
            g.setColour(colour::kCurve.withAlpha(eq_on_ ? .92f : .25f));
            g.strokePath(sum, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // nodes
        for (size_t band = 0; band < zlp::kBandNum; ++band) {
            const auto& bd = bands_[band];
            if (!bd.isUsed()) {
                continue;
            }
            const auto active = hover_ == static_cast<int>(band) || drag_ == static_cast<int>(band);
            const auto r = active ? 8.f : 7.f;
            const auto area = juce::Rectangle<float>(r * 2.f, r * 2.f).withCentre(nodePosition(band));
            const auto c = colour::kBands[band];
            g.setColour(bd.isOn() ? c : colour::kGraph);
            g.fillEllipse(area);
            g.setColour(c);
            g.drawEllipse(area, 1.5f);
            g.setColour(bd.isOn() ? colour::kWindow : c);
            g.setFont(Fonts::get().font(9.f, Fonts::kSemiBold));
            g.drawText(juce::String(band + 1), area.translated(0.f, .5f), juce::Justification::centred);
        }

        if (!hasBands()) {
            g.setFont(Fonts::get().font(type::kSmall));
            g.setColour(colour::kTextTertiary);
            g.drawText("Double-click to add a band", juce::Rectangle<float>(b.getRight() - 212.f, 8.f, 200.f, 16.f),
                       juce::Justification::centredRight);
        }

        // the hovered band's values
        const auto shown = drag_ >= 0 ? drag_ : hover_;
        if (shown >= 0) {
            const auto& bd = bands_[static_cast<size_t>(shown)];
            std::vector<std::pair<juce::String, juce::String>> rows{
                {
                    zlp::PFilterType::kChoices[bd.type],
                    hasSlope(bd.type) ? zlp::POrder::kChoices[bd.order] : juce::String()
                },
                {"Freq", ui::format::freq(bd.freq)},
            };
            if (bd.hasGain()) {
                rows.emplace_back("Gain", formats::signedDB(bd.gain));
            }
            if (!hasSlope(bd.type)) {
                rows.emplace_back("Q", ui::format::q(bd.q));
            }
            graph::drawReadout(g, nodePosition(static_cast<size_t>(shown)), rows, b);
        }
    }

    void SideEqView::setBand(const size_t band, const std::initializer_list<std::pair<const char*, float>> values,
                             const juce::String& name) {
        std::vector<std::pair<juce::RangedAudioParameter*, float>> list;
        for (const auto& [id, v] : values) {
            list.emplace_back(para(id, band), v);
        }
        bridge_.getEditor().setNow(list, name);
    }

    void SideEqView::mouseMove(const juce::MouseEvent& e) {
        hover_ = bandAt(e.position);
        setMouseCursor(hover_ >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void SideEqView::mouseExit(const juce::MouseEvent&) {
        if (drag_ < 0) {
            hover_ = -1;
            repaint();
        }
    }

    void SideEqView::mouseDown(const juce::MouseEvent& e) {
        timerCallback();
        const auto band = bandAt(e.position);
        if (band < 0) {
            return;
        }
        const auto i = static_cast<size_t>(band);
        if (e.mods.isPopupMenu()) {
            showMenu(i);
            return;
        }
        if (e.mods.isAltDown()) {
            setBand(i, {{zlp::PFilterStatus::kID, bands_[i].isOn() ? 1.f : 2.f}},
                    bands_[i].isOn() ? "Bypass Band" : "Enable Band");
            return;
        }
        drag_ = band;
        drag_start_ = e.position;
        drag_start_freq_ = bands_[i].freq;
        drag_start_gain_ = bands_[i].gain;
        juce::RangedAudioParameter* paras[] = {para(zlp::PFreq::kID, i), para(zlp::PGain::kID, i)};
        bridge_.getEditor().beginEdit(paras, "Move Side-Chain Band");
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void SideEqView::mouseDrag(const juce::MouseEvent& e) {
        if (drag_ < 0) {
            return;
        }
        const auto i = static_cast<size_t>(drag_);
        auto freq = freqOf(e.position.x);
        auto gain = dbOf(e.position.y);
        // Shift keeps the drag on one axis
        if (e.mods.isShiftDown()) {
            if (std::abs(e.position.x - drag_start_.x) > std::abs(e.position.y - drag_start_.y)) {
                gain = drag_start_gain_;
            } else {
                freq = drag_start_freq_;
            }
        }
        auto& editor = bridge_.getEditor();
        editor.setPlain(para(zlp::PFreq::kID, i), std::clamp(freq, 10.f, 20000.f));
        if (bands_[i].hasGain()) {
            editor.setPlain(para(zlp::PGain::kID, i), std::clamp(std::round(gain * 10.f) / 10.f, -30.f, 30.f));
        }
        tick();
    }

    void SideEqView::mouseUp(const juce::MouseEvent& e) {
        if (drag_ >= 0) {
            drag_ = -1;
            bridge_.getEditor().endEdit();
            mouseMove(e);
        }
    }

    void SideEqView::mouseDoubleClick(const juce::MouseEvent& e) {
        const auto hit = bandAt(e.position);
        if (hit >= 0) {
            // reset the band's gain, as in the EQ
            const auto i = static_cast<size_t>(hit);
            if (bands_[i].hasGain()) {
                setBand(i, {{zlp::PGain::kID, 0.f}}, "Reset Band Gain");
            }
            return;
        }
        if (!getPlot().contains(e.position)) {
            return;
        }
        for (size_t i = 0; i < zlp::kBandNum; ++i) {
            if (bands_[i].isUsed()) {
                continue;
            }
            // near the ends, a cut is the useful shape for a detector
            const auto freq = freqOf(e.position.x);
            const auto type = freq < 60.f ? FilterType::kHighPass : freq > 12000.f ? FilterType::kLowPass
                                                                                   : FilterType::kPeak;
            const auto gain = type == FilterType::kPeak ? std::round(dbOf(e.position.y) * 10.f) / 10.f : 0.f;
            setBand(i, {
                        {zlp::PFilterType::kID, static_cast<float>(type)},
                        {zlp::POrder::kID, 1.f},
                        {zlp::PFreq::kID, std::clamp(freq, 10.f, 20000.f)},
                        {zlp::PGain::kID, std::clamp(gain, -30.f, 30.f)},
                        {zlp::PQ::kID, .707f},
                        {zlp::PFilterStatus::kID, 2.f},
                    }, "Add Side-Chain Band");
            hover_ = static_cast<int>(i);
            tick();
            return;
        }
    }

    void SideEqView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
        const auto band = bandAt(e.position);
        if (band < 0 || drag_ >= 0) {
            return;
        }
        const auto i = static_cast<size_t>(band);
        const auto delta = (std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX)
                           * (wheel.isReversed ? -1.f : 1.f);
        if (std::abs(delta) < 1e-6f) {
            return;
        }
        if (hasSlope(bands_[i].type)) {
            // slopes step, one undo step each
            timerCallback();
            const auto order = std::clamp(bands_[i].order + (delta > 0.f ? 1 : -1), 0,
                                          zlp::POrder::kChoices.size() - 1);
            setBand(i, {{zlp::POrder::kID, static_cast<float>(order)}}, "Change Band Slope");
            return;
        }
        if (wheel_band_ != band) {
            timerCallback();
            wheel_band_ = band;
            juce::RangedAudioParameter* paras[] = {para(zlp::PQ::kID, i)};
            bridge_.getEditor().beginEdit(paras, "Change Band Q");
        }
        auto* q = para(zlp::PQ::kID, i);
        const auto norm = std::clamp(q->getValue() + delta * .04f, 0.f, 1.f);
        bridge_.getEditor().setPlain(q, q->convertFrom0to1(norm));
        startTimer(450);
        tick();
    }

    void SideEqView::timerCallback() {
        stopTimer();
        if (wheel_band_ >= 0) {
            wheel_band_ = -1;
            bridge_.getEditor().endEdit();
        }
    }

    void SideEqView::showMenu(const size_t band) {
        const auto& b = bands_[band];
        juce::PopupMenu menu;
        menu.addSectionHeader("Band " + juce::String(band + 1));
        // the shapes that make sense for a detector
        constexpr std::array kTypes{
            FilterType::kPeak, FilterType::kLowShelf, FilterType::kHighShelf, FilterType::kTiltShelf,
            FilterType::kHighPass, FilterType::kLowPass, FilterType::kBandPass, FilterType::kNotch
        };
        for (const auto t : kTypes) {
            menu.addItem(100 + static_cast<int>(t), zlp::PFilterType::kChoices[static_cast<int>(t)], true, b.type == t);
        }
        if (hasSlope(b.type)) {
            juce::PopupMenu slopes;
            for (int i = 0; i < zlp::POrder::kChoices.size(); ++i) {
                slopes.addItem(200 + i, zlp::POrder::kChoices[i], true, b.order == i);
            }
            menu.addSubMenu("Slope", slopes);
        }
        menu.addSeparator();
        menu.addItem(1, b.isOn() ? "Bypass band" : "Enable band");
        menu.addItem(2, "Delete band");
        ui::showPopupMenu(menu, *this, juce::PopupMenu::Options().withTargetScreenArea(
                              localAreaToGlobal(juce::Rectangle<int>(1, 1).withCentre(
                                  nodePosition(band).toInt()))),
                          [safe = juce::Component::SafePointer(this), band](const int result) {
                              if (safe == nullptr || result == 0) {
                                  return;
                              }
                              if (result == 1) {
                                  safe->setBand(band, {{zlp::PFilterStatus::kID, safe->bands_[band].isOn() ? 1.f : 2.f}},
                                                "Bypass Band");
                              } else if (result == 2) {
                                  safe->setBand(band, {{zlp::PFilterStatus::kID, 0.f}}, "Delete Band");
                              } else if (result >= 200) {
                                  safe->setBand(band, {{zlp::POrder::kID, static_cast<float>(result - 200)}},
                                                "Change Band Slope");
                              } else if (result >= 100) {
                                  const auto type = result - 100;
                                  const auto gain = type == FilterType::kPeak || type == FilterType::kLowShelf
                                                    || type == FilterType::kHighShelf || type == FilterType::kTiltShelf;
                                  if (gain) {
                                      safe->setBand(band, {{zlp::PFilterType::kID, static_cast<float>(type)}},
                                                    "Change Band Shape");
                                  } else {
                                      safe->setBand(band, {
                                                        {zlp::PFilterType::kID, static_cast<float>(type)},
                                                        {zlp::PGain::kID, 0.f}
                                                    }, "Change Band Shape");
                                  }
                              }
                              safe->tick();
                          });
    }
}
