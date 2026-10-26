// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "model/preset_codec.hpp"
#include "theme/eq_tokens.hpp"

namespace altvane::eq {
    using namespace tokens;

    namespace {
        constexpr auto kSpectrumGrab = "spectrum_grab";

        ShellConfig makeConfig() {
            ShellConfig config;
            config.product = "EQ";
            config.window = {
                size::kDefaultWidth, size::kDefaultHeight, size::kMinWidth, size::kMinHeight,
                size::kMaxWidth, size::kMaxHeight
            };
            config.preset_codec = presetCodec();
            config.credit = juce::String(juce::CharPointer_UTF8("Based on ZL Equalizer by ZL Audio  \xc2\xb7  AGPL-3.0"));
            config.switches.push_back({"ANALYZER", "Spectrum Grab", kSpectrumGrab, true});
            return config;
        }
    }

    Editor::Editor(PluginProcessor& p) :
        EditorShell(p, p.parameters_, p.session_, makeConfig()),
        p_ref_(p),
        bridge_(p, param_editor_),
        match_(bridge_),
        spectrum_(p, &match_),
        graph_(bridge_, selection_, response_, spectrum_),
        band_panel_(bridge_, selection_),
        footer_(bridge_),
        rail_(bridge_, spectrum_),
        match_panel_(match_) {
        graph_.setSpectrumGrabEnabled(getUISettings().getSwitch(kSpectrumGrab));

        root_.addAndMakeVisible(graph_);
        root_.addAndMakeVisible(footer_);
        root_.addAndMakeVisible(rail_);
        root_.addChildComponent(band_panel_);
        root_.addChildComponent(match_panel_);
        graph_.setMatchEngine(&match_);
        match_.on_state_changed = [this] {
            match_panel_.sync();
            footer_.tick();
            graph_.repaint();
            if (match_.getState() == MatchEngine::State::kOff) {
                if (graph_.isShowing()) {
                    graph_.grabKeyboardFocus();
                }
            }
        };
        footer_.on_toggle_match = [this] { toggleMatch(); };
        footer_.is_matching = [this] { return match_.getState() != MatchEngine::State::kOff; };

        band_panel_.on_solo = [this](const size_t band, const bool side) { graph_.setSolo(band, side); };
        band_panel_.get_solo = [this] { return graph_.getSoloBand(); };
        band_panel_.get_solo_side = [this] { return graph_.isSoloSideChain(); };

        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
        match_.on_state_changed = nullptr;
        match_.cancel();
        spectrum_.stop();
        p_ref_.getController().setEditorON(false);
    }

    void Editor::layout(const juce::Rectangle<int> area) {
        auto b = area;
        footer_.setBounds(b.removeFromBottom(static_cast<int>(size::kFooterHeight)));
        rail_.setBounds(b.removeFromRight(static_cast<int>(size::kRailWidth)));
        graph_.setBounds(b);
        panel_was_visible_ = false;
        layoutPanel(0.0);
    }

    void Editor::showingChanged(const bool showing) {
        p_ref_.getController().setEditorON(showing);
        if (showing) {
            spectrum_.start();
        } else {
            spectrum_.stop();
        }
    }

    void Editor::settingsApplied() {
        graph_.setSpectrumGrabEnabled(getUISettings().getSwitch(kSpectrumGrab));
    }

    void Editor::tick(const double dt) {
        spectrum_.setRefreshRate(getFrameRate());

        bridge_.refresh();
        graph_.tick(dt);
        layoutPanel(dt);
        layoutMatchPanel(dt);
        if (bridge_.haveGlobalsChanged()) {
            footer_.tick();
        }
        rail_.tick(dt);
        spectrum_.trigger();
        bridge_.clearChanges();
    }

    void Editor::toggleMatch() {
        if (match_.getState() == MatchEngine::State::kOff) {
            graph_.setSolo(kBandNum);
            selection_.clear();
            match_.start();
        } else {
            match_.cancel();
        }
    }

    void Editor::layoutMatchPanel(const double dt) {
        const auto wants = match_.getState() != MatchEngine::State::kOff;
        match_fade_.setTarget(wants);
        match_fade_.update(static_cast<float>(dt));
        const auto alpha = match_fade_.getEased();
        if (alpha <= 0.f && !wants) {
            match_panel_.setVisible(false);
            return;
        }
        const auto graph_bounds = graph_.getBounds();
        const auto w = std::min(match_panel_.getIdealWidth(), graph_bounds.getWidth() - 20);
        const auto h = match_panel_.getIdealHeight();
        const auto bounds = juce::Rectangle<int>(w, h).withCentre({graph_bounds.getCentreX(), 0})
                                                      .withBottomY(graph_bounds.getBottom() - 12
                                                                   + juce::roundToInt((1.f - alpha) * 10.f));
        if (bounds != match_panel_.getBounds()) {
            match_panel_.setBounds(bounds);
        }
        match_panel_.setAlpha(alpha);
        match_panel_.setVisible(true);
        match_panel_.setInterceptsMouseClicks(wants, wants);
    }

    void Editor::layoutPanel(const double dt) {
        const auto wants = band_panel_.tick() && match_.getState() == MatchEngine::State::kOff;
        panel_fade_.setTarget(wants);
        panel_fade_.update(static_cast<float>(dt));
        const auto alpha = panel_fade_.getEased();
        if (alpha <= 0.f && !wants) {
            band_panel_.setVisible(false);
            panel_was_visible_ = false;
            return;
        }
        const auto graph_bounds = graph_.getBounds().toFloat();
        const auto w = static_cast<float>(band_panel_.getIdealWidth());
        const auto h = static_cast<float>(band_panel_.getIdealHeight());
        if (wants) {
            const auto node_x = graph_bounds.getX() + graph_.getNodeX(selection_.getPrimary());
            const auto x = std::clamp(node_x - w * .5f, graph_bounds.getX() + 10.f,
                                      std::max(graph_bounds.getRight() - w - 10.f, graph_bounds.getX() + 10.f));
            panel_x_.setTarget(x);
            if (!panel_was_visible_) {
                panel_x_.snap(x);
            }
        }
        panel_x_.update(static_cast<float>(dt));
        const auto slide = (1.f - alpha) * 10.f;
        const auto y = graph_bounds.getBottom() - h - 12.f + slide;
        const auto bounds = juce::Rectangle<float>(panel_x_.getValue(), y, w, h).toNearestInt();
        if (bounds.getWidth() != band_panel_.getWidth() || bounds.getHeight() != band_panel_.getHeight()) {
            band_panel_.setBounds(bounds);
        } else if (bounds.getPosition() != band_panel_.getPosition()) {
            band_panel_.setTopLeftPosition(bounds.getPosition());
        }
        band_panel_.setAlpha(alpha);
        band_panel_.setVisible(true);
        band_panel_.setInterceptsMouseClicks(wants, wants);
        panel_was_visible_ = true;
        graph_.setBottomInset(alpha * (h + 12.f));
    }

    int Editor::getControlParameterIndex(juce::Component& component) {
        if (&component == &graph_ || component.isParentOf(&graph_)) {
            // over the graph, the hovered (or selected) band's frequency
            auto band = selection_.getHovered();
            if (band >= kBandNum) {
                band = selection_.getPrimary();
            }
            return band < kBandNum ? bridge_.params(band)[BandParams::kFreq]->getParameterIndex() : -1;
        }
        return EditorShell::getControlParameterIndex(component);
    }

    bool Editor::keyPressed(const juce::KeyPress& key) {
        const auto mods = key.getModifiers();
        if (mods.isCommandDown() && mods.isAltDown() && (key.getKeyCode() == 'F' || key.getKeyCode() == 'f')) {
            graph_.togglePerformanceOverlay();
            return true;
        }
        return EditorShell::keyPressed(key);
    }
}
