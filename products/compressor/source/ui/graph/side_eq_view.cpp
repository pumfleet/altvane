// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "side_eq_view.hpp"

#include "altvane_ui/session/param_editor.hpp"
#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/theme/look_and_feel.hpp"
#include "altvane_ui/util/value_format.hpp"
#include "altvane_ui/widgets/help_balloon.hpp"
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
        // the shapes that make sense for a detector
        constexpr std::array kTypes{
            FilterType::kPeak, FilterType::kLowShelf, FilterType::kHighShelf, FilterType::kTiltShelf,
            FilterType::kHighPass, FilterType::kLowPass, FilterType::kBandPass, FilterType::kNotch
        };
        // the band menu's items: 100 + shape, 200 + slope
        enum MenuItem { kBypassItem = 1, kDeleteItem, kSoloItem, kShapeItems = 100, kSlopeItems = 200 };
        // the strip of controls along the top
        constexpr int kStripY = 8, kStripHeight = 22, kStripGap = 6;

        juce::String freqLabel(const float f) {
            return f >= 1000.f ? juce::String(juce::roundToInt(f / 1000.f)) + "k" : juce::String(juce::roundToInt(f));
        }

        bool hasGainType(const int type) {
            return type == FilterType::kPeak || type == FilterType::kLowShelf || type == FilterType::kHighShelf
                   || type == FilterType::kTiltShelf;
        }

        bool hasSlopeType(const int type) {
            return type == FilterType::kLowPass || type == FilterType::kHighPass;
        }

        // how far either side of its centre a band with this Q reaches, as a frequency ratio
        double halfRatioFromQ(const double q) {
            return (1.0 + std::sqrt(1.0 + 4.0 * q * q)) / (2.0 * q);
        }

        juce::String bandName(const size_t band) {
            return "Band " + juce::String(band + 1);
        }
    }

    bool SideEqView::Band::hasGain() const {
        return hasGainType(type);
    }

    bool SideEqView::Band::hasSlope() const {
        return hasSlopeType(type);
    }

    SideEqView::SideEqView(CompBridge& bridge, SideSpectrum& spectrum) : bridge_(bridge), spectrum_(spectrum) {
        setOpaque(true);
        setWantsKeyboardFocus(true);
        setTitle("Side-chain EQ");
        setDescription("Double-click, or press Return, to add a band. Click a band, or press 1 to 8, to select it: "
            "drag it or use the arrow keys to move it, scroll for Q, Delete removes it, B bypasses it and holding S "
            "solos it. Right-click a band for its shape.");

        shape_.draw_icon = [this](juce::Graphics& g, const juce::Rectangle<float> area, juce::Colour) {
            if (selected_ < 0) {
                return;
            }
            const auto band = static_cast<size_t>(selected_);
            const auto dot = juce::Rectangle<float>(13.f, 13.f).withCentre(area.getCentre());
            g.setColour(colour::kBands[band]);
            g.fillEllipse(dot);
            g.setColour(colour::kWindow);
            g.setFont(Fonts::get().font(8.5f, Fonts::kSemiBold));
            g.drawText(juce::String(band + 1), dot.translated(0.f, .5f), juce::Justification::centred);
        };
        shape_.build_menu = [this] {
            return selected_ >= 0 ? buildMenu(static_cast<size_t>(selected_)) : juce::PopupMenu();
        };
        shape_.on_result = [this](const int result) {
            if (selected_ >= 0) {
                applyMenu(static_cast<size_t>(selected_), result);
            }
        };
        ui::setHelp(shape_, "Band", "The band's shape, slope, solo, bypass and delete.");

        slope_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = selected_ >= 0 ? bands_[static_cast<size_t>(selected_)].order : -1;
            for (int i = 0; i < zlp::POrder::kChoices.size(); ++i) {
                menu.addItem(kSlopeItems + i, zlp::POrder::kChoices[i], true, i == current);
            }
            return menu;
        };
        slope_.on_result = shape_.on_result;
        ui::setHelp(slope_, "Slope", "How steeply the cut falls off.");

        for (auto* f : {&freq_, &gain_, &q_}) {
            f->setFramed(true);
        }
        ui::setHelp(freq_, "Frequency", "The band's frequency. Drag, scroll or double-click to type, e.g. 2.5k.");
        ui::setHelp(gain_, "Gain", "How much the band boosts or cuts what the detector hears.");
        ui::setHelp(q_, "Q", "The band's width: higher is narrower.");

        solo_.setOnColour(colour::kWarning);
        solo_.on_click = [this] {
            if (selected_ >= 0) {
                const auto band = static_cast<size_t>(selected_);
                setSolo(solo_band_ == band ? zlp::kBandNum : band);
            }
        };
        ui::setHelp(solo_, "Solo", "Hear the side chain in this band's range instead of the output. Hold S over "
                    "the EQ to listen only while the key is down.");

        for (auto* c : std::initializer_list<juce::Component*>{&shape_, &freq_, &gain_, &q_, &slope_, &solo_}) {
            addChildComponent(c);
        }
    }

    SideEqView::~SideEqView() {
        endTimedEdit();
        setSolo(zlp::kBandNum);
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
        layoutStrip();
    }

    void SideEqView::setToolsInset(const int x) {
        if (x != tools_inset_) {
            tools_inset_ = x;
            layoutStrip();
        }
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
        // a band removed (by undo, a preset, the host) takes its selection and solo with it
        if (selected_ >= 0 && !bands_[static_cast<size_t>(selected_)].isUsed()) {
            select(-1);
        }
        if (solo_band_ < zlp::kBandNum && !bands_[solo_band_].isUsed()) {
            setSolo(zlp::kBandNum);
        }
        if (needs_response_) {
            updateResponse();
        }
        updateStrip();
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

    std::pair<float, float> SideEqView::getSoloRange(const size_t band) const {
        // what the engine's solo filter passes: below a high-pass or low shelf, above a low-pass or high shelf,
        // around anything else
        const auto& b = bands_[band];
        const auto plot = getPlot();
        const auto fx = xOf(std::clamp(b.freq, kMinFreq, kMaxFreq));
        switch (b.type) {
        case FilterType::kHighPass:
        case FilterType::kLowShelf:
            return {plot.getX(), fx};
        case FilterType::kLowPass:
        case FilterType::kHighShelf:
            return {fx, plot.getRight()};
        case FilterType::kTiltShelf:
            return {plot.getX(), plot.getRight()};
        default: {
            const auto k = static_cast<float>(halfRatioFromQ(std::max(static_cast<double>(b.q), .1)));
            return {xOf(std::clamp(b.freq / k, kMinFreq, kMaxFreq)), xOf(std::clamp(b.freq * k, kMinFreq, kMaxFreq))};
        }
        }
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
                const auto active = hover_ == static_cast<int>(band) || drag_ == static_cast<int>(band)
                                    || selected_ == static_cast<int>(band);
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

        // a soloed band: everything outside what it lets through steps back
        if (solo_band_ < zlp::kBandNum) {
            const auto [lx, rx] = getSoloRange(solo_band_);
            const auto area = plot.withTop(0.f).withBottom(b.getBottom());
            g.setColour(juce::Colours::black.withAlpha(.45f));
            g.fillRect(area.withRight(std::max(lx, area.getX())));
            g.fillRect(area.withLeft(std::min(rx, area.getRight())));
            g.setColour(colour::kBands[solo_band_].withAlpha(.06f));
            g.fillRect(area.withLeft(lx).withRight(std::max(lx, rx)));
        }

        // nodes
        for (size_t band = 0; band < zlp::kBandNum; ++band) {
            const auto& bd = bands_[band];
            if (!bd.isUsed()) {
                continue;
            }
            const auto active = hover_ == static_cast<int>(band) || drag_ == static_cast<int>(band);
            const auto r = active ? 8.f : 7.f;
            const auto centre = nodePosition(band);
            const auto area = juce::Rectangle<float>(r * 2.f, r * 2.f).withCentre(centre);
            const auto c = colour::kBands[band];
            if (selected_ == static_cast<int>(band)) {
                g.setColour(c.withAlpha(.55f));
                g.drawEllipse(juce::Rectangle<float>(22.f, 22.f).withCentre(centre), 1.5f);
            }
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
                {zlp::PFilterType::kChoices[bd.type], bd.hasSlope() ? zlp::POrder::kChoices[bd.order] : juce::String()},
                {"Freq", ui::format::freq(bd.freq)},
            };
            if (bd.hasGain()) {
                rows.emplace_back("Gain", formats::signedDB(bd.gain));
            }
            if (!bd.hasSlope()) {
                rows.emplace_back("Q", ui::format::q(bd.q));
            }
            graph::drawReadout(g, nodePosition(static_cast<size_t>(shown)), rows, b);
        }

        if (focus_ring_) {
            g.setColour(colour::kAccent.withAlpha(.85f));
            g.drawRoundedRectangle(plot.expanded(4.f).reduced(.75f), size::kRadiusSmall, 1.5f);
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

    int SideEqView::addBand(const float freq, const float gain) {
        for (size_t i = 0; i < zlp::kBandNum; ++i) {
            if (bands_[i].isUsed()) {
                continue;
            }
            // near the ends, a cut is the useful shape for a detector
            const auto type = freq < 60.f ? FilterType::kHighPass : freq > 12000.f ? FilterType::kLowPass
                                                                                   : FilterType::kPeak;
            const auto band_gain = type == FilterType::kPeak ? std::round(gain * 10.f) / 10.f : 0.f;
            setBand(i, {
                        {zlp::PFilterType::kID, static_cast<float>(type)},
                        {zlp::POrder::kID, 1.f},
                        {zlp::PFreq::kID, std::clamp(freq, 10.f, 20000.f)},
                        {zlp::PGain::kID, std::clamp(band_gain, -30.f, 30.f)},
                        {zlp::PQ::kID, .707f},
                        {zlp::PFilterStatus::kID, 2.f},
                    }, "Add Side-Chain Band");
            tick();
            select(static_cast<int>(i));
            return static_cast<int>(i);
        }
        return -1;
    }

    void SideEqView::select(int band) {
        if (band < 0 || band >= static_cast<int>(zlp::kBandNum) || !bands_[static_cast<size_t>(band)].isUsed()) {
            band = -1;
        }
        if (band == selected_) {
            return;
        }
        endTimedEdit();
        // a latched solo belongs to its band, as in the EQ
        if (solo_band_ < zlp::kBandNum && static_cast<int>(solo_band_) != band) {
            key_solo_ = false;
            setSolo(zlp::kBandNum);
        }
        selected_ = band;
        bindStrip();
        updateStrip();
        notifySelection();
        repaint();
    }

    void SideEqView::setSolo(const size_t band) {
        const auto solo = band < zlp::kBandNum && bands_[band].isUsed() ? band : zlp::kBandNum;
        if (solo == solo_band_) {
            return;
        }
        solo_band_ = solo;
        bridge_.getProcessor().getEqualizeController().setSoloBand(solo_band_);
        solo_.setOn(selected_ >= 0 && solo_band_ == static_cast<size_t>(selected_));
        notifySelection();
        repaint();
    }

    void SideEqView::bindStrip() {
        if (selected_ < 0) {
            return;
        }
        const auto i = static_cast<size_t>(selected_);
        auto& editor = bridge_.getEditor();
        const auto name = bandName(i);
        ui::bindValue(freq_, editor, para(zlp::PFreq::kID, i), "Freq");
        freq_.format = [](const float v) { return ui::format::freq(v); };
        freq_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = ui::format::parseFreq(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        ui::bindValue(gain_, editor, para(zlp::PGain::kID, i), "Gain");
        gain_.format = formats::signedDB;
        ui::bindValue(q_, editor, para(zlp::PQ::kID, i), "Q");
        q_.format = [](const float v) { return ui::format::q(v); };
        for (auto* f : {&freq_, &gain_, &q_}) {
            f->setAccent(colour::kBands[i]);
        }
        freq_.setTitle(name + " frequency");
        gain_.setTitle(name + " gain");
        q_.setTitle(name + " Q");
        shape_.setComponentID(zlp::PFilterType::kID + juce::String(i));
        shape_.setTitle(name + " shape");
        slope_.setComponentID(zlp::POrder::kID + juce::String(i));
        slope_.setTitle(name + " slope");
        solo_.setTitle("Solo " + name.toLowerCase());
    }

    void SideEqView::updateStrip() {
        const auto shown = selected_ >= 0;
        const auto& b = bands_[static_cast<size_t>(std::max(selected_, 0))];
        const std::array<std::pair<juce::Component*, bool>, 6> visibility{
            {
                {&shape_, shown}, {&freq_, shown}, {&gain_, shown && b.hasGain()}, {&q_, shown && !b.hasSlope()},
                {&slope_, shown && b.hasSlope()}, {&solo_, shown}
            }
        };
        auto changed = false;
        for (const auto& [c, visible] : visibility) {
            if (c->isVisible() != visible) {
                c->setVisible(visible);
                changed = true;
            }
        }
        if (changed) {
            layoutStrip();
        }
        if (!shown) {
            return;
        }
        shape_.setText(zlp::PFilterType::kChoices[b.type]);
        slope_.setText(zlp::POrder::kChoices[b.order]);
        freq_.setValue(b.freq);
        gain_.setValue(b.gain);
        q_.setValue(b.q);
        solo_.setOn(solo_band_ == static_cast<size_t>(selected_));
    }

    void SideEqView::layoutStrip() {
        auto x = tools_inset_;
        const auto place = [&](juce::Component& c, const int width) {
            if (c.isVisible()) {
                c.setBounds(x, kStripY, width, kStripHeight);
                x += width + kStripGap;
            }
        };
        place(shape_, 124);
        place(freq_, 116);
        place(gain_, 100);
        place(q_, 76);
        place(slope_, 104);
        place(solo_, solo_.getIdealWidth() + 8);
    }

    juce::String SideEqView::describeSelection() const {
        if (selected_ < 0) {
            const auto used = std::count_if(bands_.begin(), bands_.end(), [](const Band& b) { return b.isUsed(); });
            return used == 0 ? juce::String("No bands")
                             : juce::String(used) + (used == 1 ? " band" : " bands") + ", none selected";
        }
        const auto i = static_cast<size_t>(selected_);
        const auto& b = bands_[i];
        juce::StringArray parts{bandName(i), zlp::PFilterType::kChoices[b.type], ui::format::freq(b.freq)};
        if (b.hasGain()) {
            parts.add(formats::signedDB(b.gain));
        }
        parts.add(b.hasSlope() ? zlp::POrder::kChoices[b.order] : "Q " + ui::format::q(b.q));
        if (!b.isOn()) {
            parts.add("bypassed");
        }
        if (solo_band_ == i) {
            parts.add("soloed");
        }
        return parts.joinIntoString(", ");
    }

    void SideEqView::notifySelection() {
        if (auto* handler = getAccessibilityHandler()) {
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
        }
    }

    std::unique_ptr<juce::AccessibilityHandler> SideEqView::createAccessibilityHandler() {
        // a group whose value reads out the selected band; pressing it adds a band
        class Value final : public juce::AccessibilityTextValueInterface {
        public:
            explicit Value(SideEqView& view) : view_(view) {
            }

            bool isReadOnly() const override { return true; }

            juce::String getCurrentValueAsString() const override { return view_.describeSelection(); }

            void setValueAsString(const juce::String&) override {
            }

        private:
            SideEqView& view_;
        };
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::group,
            juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] {
                addBand(1000.f, 0.f);
            }),
            juce::AccessibilityHandler::Interfaces{std::make_unique<Value>(*this)});
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
        endTimedEdit();
        const auto band = bandAt(e.position);
        if (band < 0) {
            if (!e.mods.isPopupMenu()) {
                select(-1);
            }
            return;
        }
        const auto i = static_cast<size_t>(band);
        select(band);
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
        if (const auto added = addBand(freqOf(e.position.x), dbOf(e.position.y)); added >= 0) {
            hover_ = added;
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
        if (bands_[i].hasSlope()) {
            // slopes step, one undo step each
            endTimedEdit();
            const auto order = std::clamp(bands_[i].order + (delta > 0.f ? 1 : -1), 0,
                                          zlp::POrder::kChoices.size() - 1);
            setBand(i, {{zlp::POrder::kID, static_cast<float>(order)}}, "Change Band Slope");
            return;
        }
        beginTimedEdit(TimedEdit::kQ, band);
        auto* q = para(zlp::PQ::kID, i);
        const auto norm = std::clamp(q->getValue() + delta * .04f, 0.f, 1.f);
        bridge_.getEditor().setPlain(q, q->convertFrom0to1(norm));
        tick();
    }

    bool SideEqView::keyPressed(const juce::KeyPress& key) {
        const auto code = key.getKeyCode();
        const auto mods = key.getModifiers();
        if (code == juce::KeyPress::returnKey) {
            if (selected_ < 0) {
                addBand(1000.f, 0.f);
            } else {
                freq_.beginTextEdit();
            }
            return true;
        }
        if (code == juce::KeyPress::escapeKey && selected_ >= 0) {
            select(-1);
            return true;
        }
        if (!mods.isAnyModifierKeyDown() && code >= '1' && code < '1' + static_cast<int>(zlp::kBandNum)) {
            select(code - '1');
            return true;
        }
        const auto arrow = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                           || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
        if (arrow && selected_ < 0) {
            // the first arrow picks the first band
            const auto first = std::find_if(bands_.begin(), bands_.end(), [](const Band& b) { return b.isUsed(); });
            select(first == bands_.end() ? -1 : static_cast<int>(std::distance(bands_.begin(), first)));
            return selected_ >= 0;
        }
        if (selected_ < 0) {
            return false;
        }
        const auto i = static_cast<size_t>(selected_);
        if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) {
            setBand(i, {{zlp::PFilterStatus::kID, 0.f}}, "Delete Band");
            tick();
            return true;
        }
        if (!mods.isAnyModifierKeyDown() && (code == 'B' || code == 'b')) {
            setBand(i, {{zlp::PFilterStatus::kID, bands_[i].isOn() ? 1.f : 2.f}},
                    bands_[i].isOn() ? "Bypass Band" : "Enable Band");
            tick();
            return true;
        }
        if (!mods.isAnyModifierKeyDown() && (code == 'S' || code == 's')) {
            key_solo_ = true;
            setSolo(i);
            return true;
        }
        // arrows move the band: a semitone sideways, half a dB up and down (Shift for finer steps)
        const auto fine = mods.isShiftDown();
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {
            const auto step = fine ? 1.f / 48.f : 1.f / 12.f;
            nudge(code == juce::KeyPress::leftKey ? -step : step, 0.f);
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) {
            const auto step = fine ? .1f : .5f;
            nudge(0.f, code == juce::KeyPress::downKey ? -step : step);
            return true;
        }
        return false;
    }

    bool SideEqView::keyStateChanged(const bool is_key_down) {
        if (!is_key_down && key_solo_ && !juce::KeyPress::isKeyCurrentlyDown('S')
            && !juce::KeyPress::isKeyCurrentlyDown('s')) {
            key_solo_ = false;
            setSolo(zlp::kBandNum);
            return true;
        }
        return false;
    }

    void SideEqView::focusGainedWithDirection(const FocusChangeType cause, FocusChangeDirection) {
        focus_ring_ = cause == focusChangedByTabKey;
        // arriving by Tab, the keyboard works on the first band straight away
        if (focus_ring_ && selected_ < 0) {
            const auto first = std::find_if(bands_.begin(), bands_.end(), [](const Band& b) { return b.isUsed(); });
            if (first != bands_.end()) {
                select(static_cast<int>(std::distance(bands_.begin(), first)));
            }
        }
        repaint();
    }

    void SideEqView::focusLost(FocusChangeType) {
        focus_ring_ = false;
        // the key that held a solo may be released elsewhere
        if (key_solo_) {
            key_solo_ = false;
            setSolo(zlp::kBandNum);
        }
        repaint();
    }

    void SideEqView::nudge(const float octaves, const float db) {
        const auto i = static_cast<size_t>(selected_);
        const auto& b = bands_[i];
        if (db != 0.f && !b.hasGain()) {
            return;
        }
        beginTimedEdit(TimedEdit::kMove, selected_);
        auto& editor = bridge_.getEditor();
        if (octaves != 0.f) {
            editor.setPlain(para(zlp::PFreq::kID, i), std::clamp(b.freq * std::exp2(octaves), 10.f, 20000.f));
        }
        if (db != 0.f) {
            editor.setPlain(para(zlp::PGain::kID, i), std::clamp(std::round((b.gain + db) * 10.f) / 10.f, -30.f, 30.f));
        }
        tick();
        notifySelection();
    }

    void SideEqView::beginTimedEdit(const TimedEdit kind, const int band) {
        auto& editor = bridge_.getEditor();
        // the same burst carries on, unless another control has taken the edit over since
        if (timed_edit_ != kind || timed_band_ != band || !editor.isEditing(timed_edit_id_)) {
            endTimedEdit();
            const auto i = static_cast<size_t>(band);
            if (kind == TimedEdit::kQ) {
                juce::RangedAudioParameter* paras[] = {para(zlp::PQ::kID, i)};
                editor.beginEdit(paras, "Change Band Q");
            } else {
                juce::RangedAudioParameter* paras[] = {para(zlp::PFreq::kID, i), para(zlp::PGain::kID, i)};
                editor.beginEdit(paras, "Move Side-Chain Band");
            }
            timed_edit_ = kind;
            timed_band_ = band;
            timed_edit_id_ = editor.getEditId();
        }
        startTimer(450);
    }

    void SideEqView::endTimedEdit() {
        stopTimer();
        if (timed_edit_ != TimedEdit::kNone) {
            if (bridge_.getEditor().isEditing(timed_edit_id_)) {
                bridge_.getEditor().endEdit();
            }
            timed_edit_ = TimedEdit::kNone;
            timed_band_ = -1;
        }
    }

    void SideEqView::timerCallback() {
        endTimedEdit();
    }

    juce::PopupMenu SideEqView::buildMenu(const size_t band) const {
        const auto& b = bands_[band];
        juce::PopupMenu menu;
        menu.addSectionHeader(bandName(band));
        for (const auto t : kTypes) {
            menu.addItem(kShapeItems + static_cast<int>(t), zlp::PFilterType::kChoices[static_cast<int>(t)], true,
                         b.type == t);
        }
        if (b.hasSlope()) {
            juce::PopupMenu slopes;
            for (int i = 0; i < zlp::POrder::kChoices.size(); ++i) {
                slopes.addItem(kSlopeItems + i, zlp::POrder::kChoices[i], true, b.order == i);
            }
            menu.addSubMenu("Slope", slopes);
        }
        menu.addSeparator();
        menu.addItem(kSoloItem, solo_band_ == band ? "Stop solo" : "Solo band");
        menu.addItem(kBypassItem, b.isOn() ? "Bypass band" : "Enable band");
        menu.addItem(kDeleteItem, "Delete band");
        return menu;
    }

    void SideEqView::applyMenu(const size_t band, const int result) {
        if (result == kSoloItem) {
            setSolo(solo_band_ == band ? zlp::kBandNum : band);
        } else if (result == kBypassItem) {
            setBand(band, {{zlp::PFilterStatus::kID, bands_[band].isOn() ? 1.f : 2.f}}, "Bypass Band");
        } else if (result == kDeleteItem) {
            setBand(band, {{zlp::PFilterStatus::kID, 0.f}}, "Delete Band");
        } else if (result >= kSlopeItems) {
            setBand(band, {{zlp::POrder::kID, static_cast<float>(result - kSlopeItems)}}, "Change Band Slope");
        } else if (result >= kShapeItems) {
            const auto type = result - kShapeItems;
            if (hasGainType(type)) {
                setBand(band, {{zlp::PFilterType::kID, static_cast<float>(type)}}, "Change Band Shape");
            } else {
                setBand(band, {{zlp::PFilterType::kID, static_cast<float>(type)}, {zlp::PGain::kID, 0.f}},
                        "Change Band Shape");
            }
        }
        tick();
    }

    void SideEqView::showMenu(const size_t band) {
        ui::showPopupMenu(buildMenu(band), *this, juce::PopupMenu::Options().withTargetScreenArea(
                              localAreaToGlobal(juce::Rectangle<int>(1, 1).withCentre(nodePosition(band).toInt()))),
                          [safe = juce::Component::SafePointer(this), band](const int result) {
                              if (safe != nullptr && result != 0) {
                                  safe->applyMenu(band, result);
                              }
                          });
    }
}
