// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "visualiser.hpp"

#include <altvane_ui/theme/fonts.hpp>

#include "theme/dialogue_tokens.hpp"

namespace altvane::dialogue {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        constexpr float kTopDb = -18.f;
        constexpr float kBottomDb = -96.f;
        // display tilt, so speech reads level across the band (dB per octave around 1 kHz)
        constexpr float kTiltDb = 3.f;
        constexpr float kReleaseDbPerSecond = 60.f;
        constexpr float kSilenceDb = -100.f;

        float log2Ratio(const float hz) {
            return std::log2(hz / dsp::AnalysisFrame::kLowHz);
        }

        juce::String formatHz(const float hz) {
            return hz >= 1000.f ? juce::String(hz / 1000.f, hz >= 10000.f ? 0 : 1) + " kHz"
                                : juce::String(juce::roundToInt(hz)) + " Hz";
        }
    }

    Visualiser::Visualiser(PluginProcessor& p, ui::ParamEditor& editor) : p_ref_(p), editor_(editor) {
        setTitle("Spectrum");
        setDescription("The input's spectrum, split into voice, reverb and ambience");
        for (auto& stem : stem_db_) {
            stem.fill(kSilenceDb);
        }
        input_db_.fill(kSilenceDb);
        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            level_springs_[s] = ui::Spring(1.f, 8.f);
        }
    }

    // ---------------------------------------------------------------- geometry

    float Visualiser::freqX(const float hz) const {
        const auto span = log2Ratio(dsp::AnalysisFrame::kHighHz);
        return plot_.getX() + plot_.getWidth() * std::clamp(log2Ratio(hz) / span, 0.f, 1.f);
    }

    float Visualiser::bandX(const int band) const {
        const auto t = (static_cast<float>(band) + .5f) / static_cast<float>(kBands);
        return plot_.getX() + plot_.getWidth() * t;
    }

    float Visualiser::energyY(const float db) const {
        const auto t = std::clamp((kTopDb - db) / (kTopDb - kBottomDb), 0.f, 1.f);
        return plot_.getY() + t * plot_.getHeight();
    }

    float Visualiser::sensitivityY(const float value) const {
        const auto band = plot_.reduced(0.f, plot_.getHeight() * .14f);
        return band.getCentreY() - value * band.getHeight() * .5f;
    }

    float Visualiser::sensitivityValue(const float y) const {
        const auto band = plot_.reduced(0.f, plot_.getHeight() * .14f);
        return std::clamp((band.getCentreY() - y) / (band.getHeight() * .5f), -1.f, 1.f);
    }

    juce::RangedAudioParameter* Visualiser::nodeParam(const int node) const {
        if (focus_ < 0 || node < 0) {
            return nullptr;
        }
        return p_ref_.parameters_.getParameter(params::sensitivityID(static_cast<size_t>(focus_),
                                                                     static_cast<size_t>(node)));
    }

    int Visualiser::hitNode(const juce::Point<float> p) const {
        if (focus_ < 0) {
            return -1;
        }
        int best = -1;
        auto best_distance = size::kNodeHitRadius;
        for (int n = 0; n < dsp::kSensitivityNodes; ++n) {
            const juce::Point node{freqX(dsp::kSensitivityFrequencies[static_cast<size_t>(n)]),
                                   sensitivityY(nodes_[static_cast<size_t>(n)])};
            if (const auto d = node.getDistanceFrom(p); d < best_distance) {
                best_distance = d;
                best = n;
            }
        }
        return best;
    }

    int Visualiser::getHoveredParameterIndex() const {
        const auto node = dragged_node_ >= 0 ? dragged_node_ : hovered_node_;
        if (auto* para = nodeParam(node)) {
            return para->getParameterIndex();
        }
        return -1;
    }

    // ---------------------------------------------------------------- state

    void Visualiser::setFocus(const int stem) {
        if (stem == focus_) {
            return;
        }
        if (dragged_node_ >= 0) {
            editor_.endEdit();
            dragged_node_ = -1;
        }
        focus_ = stem;
        hovered_node_ = -1;
        if (stem >= 0) {
            focus_fade_.snap(false);
        }
        focus_fade_.setTarget(stem >= 0);
        if (on_focus_changed) {
            on_focus_changed(stem);
        }
        repaint();
    }

    void Visualiser::tick(const double dt) {
        bool dirty = false;
        if (p_ref_.getEngine().pullAnalysis(frame_)) {
            auto smooth = [dt](Bands& state, const std::array<float, kBands>& energy) {
                for (size_t b = 0; b < kBands; ++b) {
                    const auto octaves = std::log2(dsp::AnalysisFrame::bandCentre(static_cast<int>(b)) / 1000.f);
                    // power to dB
                    const auto db = .5f * juce::Decibels::gainToDecibels(energy[b], -280.f) + kTiltDb * octaves;
                    state[b] = db > state[b] ? state[b] + (db - state[b]) * .6f
                                             : std::max(db, state[b] - kReleaseDbPerSecond * static_cast<float>(dt));
                }
            };
            smooth(input_db_, frame_.input);
            for (size_t s = 0; s < dsp::kNumStems; ++s) {
                smooth(stem_db_[s], frame_.stems[s]);
            }
            const auto loudest = *std::max_element(frame_.input.begin(), frame_.input.end());
            silence_ = loudest < 1e-11f ? silence_ + dt : 0.0;
            dirty = true;
        } else {
            silence_ += dt;
        }
        const auto signal = silence_ < .6;
        dirty = dirty || signal != have_signal_;
        have_signal_ = signal;

        for (size_t s = 0; s < dsp::kNumStems; ++s) {
            const auto db = p_ref_.parameters_.getRawParameterValue(params::kLevelIDs[s])->load();
            level_springs_[s].setTarget(params::levelToGain(db));
            dirty = level_springs_[s].update(static_cast<float>(dt)) || dirty;
        }
        if (focus_ >= 0) {
            for (size_t n = 0; n < dsp::kSensitivityNodes; ++n) {
                const auto v = p_ref_.parameters_.getRawParameterValue(params::sensitivityID(static_cast<size_t>(focus_), n))->load();
                dirty = dirty || v != nodes_[n];
                nodes_[n] = v;
            }
        }
        dirty = focus_fade_.update(static_cast<float>(dt)) || dirty;
        if (dirty) {
            repaint();
        }
    }

    // ---------------------------------------------------------------- painting

    void Visualiser::resized() {
        plot_ = getLocalBounds().toFloat().withTrimmedBottom(22.f).withTrimmedTop(12.f);
        rebuildGrid();
    }

    void Visualiser::rebuildGrid() {
        const auto scale = juce::Component::getApproximateScaleFactorForComponent(this);
        const auto w = std::max(1, getWidth()), h = std::max(1, getHeight());
        grid_ = juce::Image(juce::Image::ARGB, juce::roundToInt(static_cast<float>(w) * scale),
                            juce::roundToInt(static_cast<float>(h) * scale), true);
        juce::Graphics g(grid_);
        g.addTransform(juce::AffineTransform::scale(scale));
        static constexpr std::array<float, 10> kLines{50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f, 30.f};
        for (const auto hz : kLines) {
            if (hz < dsp::AnalysisFrame::kLowHz * 1.01f || hz > dsp::AnalysisFrame::kHighHz * .99f) {
                continue;
            }
            const auto major = hz == 100.f || hz == 1000.f || hz == 10000.f;
            g.setColour(major ? colour::kGridMajor : colour::kGridMinor);
            g.fillRect(juce::Rectangle<float>(freqX(hz), plot_.getY(), 1.f, plot_.getHeight()));
            g.setColour(colour::kTextTertiary);
            g.setFont(Fonts::get().font(type::kCaption));
            const auto label = hz >= 1000.f ? juce::String(juce::roundToInt(hz / 1000.f)) + "k" : juce::String(juce::roundToInt(hz));
            g.drawText(label, juce::Rectangle<float>(freqX(hz) - 20.f, plot_.getBottom() + 4.f, 40.f, 14.f),
                       juce::Justification::centred);
        }
        for (float db = -30.f; db > kBottomDb; db -= 15.f) {
            g.setColour(colour::kGridMinor);
            g.fillRect(juce::Rectangle<float>(plot_.getX(), energyY(db), plot_.getWidth(), 1.f));
        }
    }

    juce::Path Visualiser::makeLevelPath(const Bands& db) const {
        // a smooth line through the band centres (quadratic segments between midpoints)
        juce::Path path;
        std::array<juce::Point<float>, kBands> pts;
        for (int b = 0; b < kBands; ++b) {
            pts[static_cast<size_t>(b)] = {bandX(b), energyY(db[static_cast<size_t>(b)])};
        }
        path.startNewSubPath(plot_.getX(), pts[0].y);
        path.lineTo(pts[0]);
        for (size_t b = 1; b < kBands; ++b) {
            const auto mid = (pts[b - 1] + pts[b]) * .5f;
            path.quadraticTo(pts[b - 1], mid);
        }
        path.lineTo(pts[kBands - 1]);
        path.lineTo(plot_.getRight(), pts[kBands - 1].y);
        return path;
    }

    void Visualiser::paintLayers(juce::Graphics& g) {
        const auto listen = p_ref_.getListen();
        // what each stem keeps, at its level, stacked from the bottom: voice, reverb, ambience
        static constexpr std::array<size_t, dsp::kNumStems> kOrder{dsp::kVoice, dsp::kReverb, dsp::kAmbience};
        std::array<Bands, dsp::kNumStems> cumulative{};
        Bands sum{};
        sum.fill(0.f);
        for (size_t i = 0; i < kOrder.size(); ++i) {
            const auto s = kOrder[i];
            auto gain = level_springs_[s].getValue();
            if (listen == dsp::Listen::kRemoved) {
                // what each stem loses
                gain = std::abs(1.f - gain);
            } else if (listen != dsp::Listen::kMix) {
                gain = static_cast<int>(listen) - 1 == static_cast<int>(s) ? 1.f : 0.f;
            }
            for (size_t b = 0; b < kBands; ++b) {
                sum[b] += std::pow(10.f, stem_db_[s][b] / 10.f) * gain * gain;
                cumulative[i][b] = 10.f * std::log10(sum[b] + 1e-14f);
            }
        }

        const auto bottom = plot_.getBottom();
        // the input: what there was
        auto input = makeLevelPath(input_db_);
        {
            auto fill = input;
            fill.lineTo(plot_.getRight(), bottom);
            fill.lineTo(plot_.getX(), bottom);
            fill.closeSubPath();
            g.setColour(juce::Colours::white.withAlpha(.025f));
            g.fillPath(fill);
        }

        // each layer fills only its own band, between its line and the one below
        for (int i = static_cast<int>(kOrder.size()) - 1; i >= 0; --i) {
            const auto s = kOrder[static_cast<size_t>(i)];
            const auto c = colour::stem(s);
            const auto line = makeLevelPath(cumulative[static_cast<size_t>(i)]);
            juce::Path band = line;
            if (i == 0) {
                band.lineTo(plot_.getRight(), bottom);
                band.lineTo(plot_.getX(), bottom);
            } else {
                // the layer below, traced backwards
                const auto& below = cumulative[static_cast<size_t>(i - 1)];
                band.lineTo(plot_.getRight(), energyY(below[kBands - 1]));
                for (int b = kBands - 1; b >= 0; --b) {
                    band.lineTo(bandX(b), energyY(below[static_cast<size_t>(b)]));
                }
                band.lineTo(plot_.getX(), energyY(below[0]));
            }
            band.closeSubPath();
            // the voice is the light: a soft fill under a bright line; the room layers are denser so they read
            const auto voice = s == dsp::kVoice;
            g.setGradientFill(juce::ColourGradient(c.withAlpha(voice ? .36f : .42f), 0.f, plot_.getY() + plot_.getHeight() * .2f,
                                                   c.withAlpha(voice ? .03f : .10f), 0.f, bottom, false));
            g.fillPath(band);
            g.setColour(c.withAlpha(voice ? .95f : .8f));
            g.strokePath(line, juce::PathStrokeType(voice ? 1.6f : 1.1f));
        }

        g.setColour(colour::kTextTertiary.withMultipliedAlpha(.6f));
        g.strokePath(input, juce::PathStrokeType(1.f));
    }

    void Visualiser::paintSensitivity(juce::Graphics& g, const float alpha) {
        const auto s = static_cast<size_t>(focus_);
        const auto c = colour::stem(s);
        const auto zero = sensitivityY(0.f);

        // zero line, dashed
        juce::Path zero_line;
        zero_line.startNewSubPath(plot_.getX(), zero);
        zero_line.lineTo(plot_.getRight(), zero);
        const float dashes[] = {3.f, 4.f};
        juce::PathStrokeType(1.f).createDashedStroke(zero_line, zero_line, dashes, 2);
        g.setColour(c.withAlpha(.35f * alpha));
        g.fillPath(zero_line);

        // the curve, as the processor interpolates it (smoothstep on a log axis, flat beyond the end nodes)
        juce::Path curve;
        const auto steps = static_cast<int>(plot_.getWidth() / 3.f);
        const auto lo = std::log2(dsp::kSensitivityFrequencies.front());
        const auto hi = std::log2(dsp::kSensitivityFrequencies.back());
        for (int i = 0; i <= steps; ++i) {
            const auto x = plot_.getX() + plot_.getWidth() * static_cast<float>(i) / static_cast<float>(steps);
            const auto t = (x - plot_.getX()) / plot_.getWidth();
            const auto hz = dsp::AnalysisFrame::kLowHz * std::pow(dsp::AnalysisFrame::kHighHz / dsp::AnalysisFrame::kLowHz, t);
            const auto lx = std::log2(hz);
            float v;
            if (lx <= lo) {
                v = nodes_.front();
            } else if (lx >= hi) {
                v = nodes_.back();
            } else {
                size_t n = 0;
                while (lx > std::log2(dsp::kSensitivityFrequencies[n + 1])) {
                    ++n;
                }
                const auto a = std::log2(dsp::kSensitivityFrequencies[n]);
                const auto b = std::log2(dsp::kSensitivityFrequencies[n + 1]);
                const auto u = (lx - a) / (b - a);
                v = nodes_[n] + (nodes_[n + 1] - nodes_[n]) * u * u * (3.f - 2.f * u);
            }
            const auto y = sensitivityY(v);
            if (i == 0) {
                curve.startNewSubPath(x, y);
            } else {
                curve.lineTo(x, y);
            }
        }
        auto fill = curve;
        fill.lineTo(plot_.getRight(), zero);
        fill.lineTo(plot_.getX(), zero);
        fill.closeSubPath();
        g.setColour(c.withAlpha(.10f * alpha));
        g.fillPath(fill);
        g.setColour(c.withAlpha(.95f * alpha));
        g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (int n = 0; n < dsp::kSensitivityNodes; ++n) {
            const juce::Point centre{freqX(dsp::kSensitivityFrequencies[static_cast<size_t>(n)]),
                                     sensitivityY(nodes_[static_cast<size_t>(n)])};
            const auto active = n == hovered_node_ || n == dragged_node_;
            const auto r = size::kNodeRadius + (active ? 1.5f : 0.f);
            const auto node = juce::Rectangle<float>(r * 2.f, r * 2.f).withCentre(centre);
            g.setColour(colour::kGraph.withAlpha(alpha));
            g.fillEllipse(node);
            g.setColour((active ? c : c.withAlpha(.9f)).withMultipliedAlpha(alpha));
            if (active) {
                g.fillEllipse(node.reduced(2.5f));
            }
            g.drawEllipse(node.reduced(.8f), 1.6f);
        }

        // what the curve means, at the plot's corners
        auto& fonts = Fonts::get();
        g.setFont(fonts.font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f));
        const auto name = juce::String(params::kStemNames[s]).toUpperCase();
        auto label = juce::Rectangle<float>(plot_.getX() + size::kS4, plot_.getY() + size::kS2, 260.f, 14.f);
        g.setColour(c.withAlpha(.9f * alpha));
        g.drawText(name + " SENSITIVITY", label, juce::Justification::centredLeft);
        g.setFont(fonts.font(type::kSmall));
        g.setColour(colour::kTextTertiary.withMultipliedAlpha(alpha));
        g.drawText(juce::String::fromUTF8("\xe2\x86\x91 more ") + juce::String(params::kStemNames[s]).toLowerCase()
                   + juce::String::fromUTF8("   \xe2\x86\x93 less"),
                   label.translated(0.f, 15.f), juce::Justification::centredLeft);

        if (dragged_node_ >= 0) {
            const auto n = static_cast<size_t>(dragged_node_);
            const auto x = freqX(dsp::kSensitivityFrequencies[n]);
            const auto y = sensitivityY(nodes_[n]);
            const auto text = formatHz(dsp::kSensitivityFrequencies[n]) + juce::String::fromUTF8("  \xc2\xb7  ")
                              + (nodes_[n] > .005f ? "+" : "") + juce::String(juce::roundToInt(nodes_[n] * 100.f)) + "%";
            const auto w = fonts.getTabularWidth(text, type::kSmall) + 16.f;
            auto bubble = juce::Rectangle<float>(w, 22.f).withCentre({x, y - 22.f});
            bubble = bubble.withX(std::clamp(bubble.getX(), plot_.getX() + 4.f, plot_.getRight() - w - 4.f));
            g.setColour(colour::kRaised);
            g.fillRoundedRectangle(bubble, size::kRadiusSmall);
            g.setColour(colour::kHairlineStrong);
            g.drawRoundedRectangle(bubble.reduced(.5f), size::kRadiusSmall, 1.f);
            g.setColour(colour::kTextPrimary);
            fonts.drawTabular(g, text, bubble, juce::Justification::centred, type::kSmall);
        }
    }

    void Visualiser::paintMessage(juce::Graphics& g) {
        juce::String text;
        auto c = colour::kTextTertiary;
        if (!p_ref_.isModelReady() && p_ref_.getEngine().getNumChannels() > 0) {
            text = "The separation model could not be loaded. Audio passes through unchanged.";
            c = colour::kWarning;
        } else if (!have_signal_) {
            text = "Play audio to see its voice, ambience and reverb";
        }
        if (text.isEmpty()) {
            return;
        }
        g.setColour(c);
        g.setFont(Fonts::get().font(type::kBody));
        g.drawText(text, plot_.withSizeKeepingCentre(plot_.getWidth(), 20.f).translated(0.f, -plot_.getHeight() * .1f),
                   juce::Justification::centred);
    }

    void Visualiser::paint(juce::Graphics& g) {
        g.fillAll(colour::kGraph);
        g.drawImage(grid_, getLocalBounds().toFloat());
        {
            juce::Graphics::ScopedSaveState save(g);
            g.reduceClipRegion(plot_.toNearestInt());
            const auto fade = focus_fade_.getEased();
            // the spectrum steps back while a curve is edited
            g.beginTransparencyLayer(1.f - .45f * fade);
            paintLayers(g);
            g.endTransparencyLayer();
        }
        if (focus_ >= 0) {
            paintSensitivity(g, focus_fade_.getEased());
        }
        paintMessage(g);
    }

    // ---------------------------------------------------------------- mouse

    void Visualiser::updateTooltip() {
        if (hovered_node_ >= 0) {
            setTooltip(formatHz(dsp::kSensitivityFrequencies[static_cast<size_t>(hovered_node_)])
                       + ": drag to change, double-click to reset");
        } else {
            setTooltip({});
        }
    }

    void Visualiser::mouseMove(const juce::MouseEvent& e) {
        if (const auto n = hitNode(e.position); n != hovered_node_) {
            hovered_node_ = n;
            setMouseCursor(n >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
            updateTooltip();
            repaint();
        }
    }

    void Visualiser::mouseExit(const juce::MouseEvent&) {
        if (hovered_node_ >= 0) {
            hovered_node_ = -1;
            updateTooltip();
            repaint();
        }
    }

    void Visualiser::mouseDown(const juce::MouseEvent& e) {
        const auto n = hitNode(e.position);
        if (n < 0) {
            return;
        }
        auto* para = nodeParam(n);
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
        if (e.mods.isCommandDown() || e.mods.isAltDown()) {
            editor_.setNow({{para, 0.f}}, "Reset Sensitivity");
            return;
        }
        dragged_node_ = n;
        drag_start_value_ = nodes_[static_cast<size_t>(n)];
        drag_start_y_ = e.position.y;
        juce::RangedAudioParameter* paras[] = {para};
        editor_.beginEdit(paras, "Change " + juce::String(params::kStemNames[static_cast<size_t>(focus_)]) + " Sensitivity");
        repaint();
    }

    void Visualiser::mouseDrag(const juce::MouseEvent& e) {
        if (dragged_node_ < 0) {
            return;
        }
        const auto fine = e.mods.isShiftDown() ? .2f : 1.f;
        const auto per_pixel = 1.f / (plot_.getHeight() * (1.f - .28f) * .5f);
        const auto v = std::clamp(drag_start_value_ + (drag_start_y_ - e.position.y) * per_pixel * fine
                                  * ui::InteractionSettings::drag_sensitivity, -1.f, 1.f);
        editor_.setPlain(nodeParam(dragged_node_), v);
        nodes_[static_cast<size_t>(dragged_node_)] = v;
        repaint();
    }

    void Visualiser::mouseUp(const juce::MouseEvent&) {
        if (dragged_node_ >= 0) {
            editor_.endEdit();
            dragged_node_ = -1;
            repaint();
        }
    }

    void Visualiser::mouseDoubleClick(const juce::MouseEvent& e) {
        if (const auto n = hitNode(e.position); n >= 0) {
            editor_.setNow({{nodeParam(n), 0.f}}, "Reset Sensitivity");
        }
    }
}
