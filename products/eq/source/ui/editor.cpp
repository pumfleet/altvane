// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "theme/eq_tokens.hpp"

namespace altvane::eq {
    using namespace tokens;

    namespace {
        std::unique_ptr<juce::PropertiesFile> openSettings() {
            juce::PropertiesFile::Options options;
            options.applicationName = "settings";
            options.filenameSuffix = ".xml";
            options.folderName = juce::String(JucePlugin_Manufacturer) + juce::File::getSeparatorString()
                + JucePlugin_Name;
            options.osxLibrarySubFolder = "Application Support";
            options.storageFormat = juce::PropertiesFile::storeAsXML;
            options.millisecondsBeforeSaving = 500;
            return std::make_unique<juce::PropertiesFile>(options);
        }
    }

    Editor::Editor(PluginProcessor& p) :
        AudioProcessorEditor(&p),
        p_ref_(p),
        bridge_(p),
        match_(bridge_),
        spectrum_(p, &match_),
        presets_(bridge_),
        graph_(bridge_, selection_, response_, spectrum_),
        band_panel_(bridge_, selection_),
        header_(bridge_, presets_),
        footer_(bridge_),
        rail_(bridge_, spectrum_),
        match_panel_(match_),
        preset_browser_(presets_) {
        settings_ = openSettings();
        ui_settings_.load(*settings_);
        ui_settings_.apply();
        laf_.refreshAccent();
        presets_.setSettings(settings_.get());
        graph_.setSpectrumGrabEnabled(ui_settings_.spectrum_grab);
        setLookAndFeel(&laf_);
        tooltip_window_.setLookAndFeel(&laf_);
        setOpaque(true);

        addAndMakeVisible(root_);
        root_.addAndMakeVisible(graph_);
        root_.addAndMakeVisible(header_);
        root_.addAndMakeVisible(footer_);
        root_.addAndMakeVisible(rail_);
        root_.addChildComponent(band_panel_);
        root_.addChildComponent(match_panel_);
        root_.addChildComponent(preset_browser_);
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
        root_.addChildComponent(settings_sheet_);
        const auto refocus = [this] {
            if (graph_.isShowing()) {
                graph_.grabKeyboardFocus();
            }
        };
        preset_browser_.on_closed = refocus;
        settings_sheet_.on_closed = refocus;
        settings_sheet_.on_change = [this](const UISettings& s) { applySettings(s); };
        settings_sheet_.on_reset_window_size = [this] { resetWindowSize(); };

        band_panel_.on_solo = [this](const size_t band, const bool side) { graph_.setSolo(band, side); };
        band_panel_.get_solo = [this] { return graph_.getSoloBand(); };
        band_panel_.get_solo_side = [this] { return graph_.isSoloSideChain(); };
        header_.on_undo = [this] { graph_.undo(); };
        header_.on_redo = [this] { graph_.redo(); };
        header_.on_open_presets = [this] {
            settings_sheet_.close();
            preset_browser_.open();
        };
        header_.on_open_settings = [this] {
            preset_browser_.close();
            settings_sheet_.setSettings(ui_settings_);
            settings_sheet_.open();
        };

        const auto ui_scale = getScale();
        const auto w = settings_->getIntValue("width", size::kDefaultWidth);
        const auto h = settings_->getIntValue("height", size::kDefaultHeight);

        setResizable(true, p.wrapperType != PluginProcessor::wrapperType_AudioUnitv3);
        applying_size_ = true;
        setResizeLimits(juce::roundToInt(size::kMinWidth * ui_scale), juce::roundToInt(size::kMinHeight * ui_scale),
                        juce::roundToInt(size::kMaxWidth * ui_scale), juce::roundToInt(size::kMaxHeight * ui_scale));
        setSize(juce::roundToInt(static_cast<float>(std::clamp(w, size::kMinWidth, size::kMaxWidth)) * ui_scale),
                juce::roundToInt(static_cast<float>(std::clamp(h, size::kMinHeight, size::kMaxHeight)) * ui_scale));
        applying_size_ = false;
        resized();
        updateShowing();
    }

    Editor::~Editor() {
        match_.on_state_changed = nullptr;
        match_.cancel();
        vblank_.reset();
        spectrum_.stop();
        p_ref_.getController().setEditorON(false);
        if (settings_ != nullptr) {
            settings_->saveIfNeeded();
        }
        tooltip_window_.setLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }

    void Editor::paint(juce::Graphics& g) {
        g.fillAll(colour::kWindow);
    }

    void Editor::resized() {
        const auto ui_scale = getScale();
        const auto logical_w = static_cast<float>(getWidth()) / ui_scale;
        const auto logical_h = static_cast<float>(getHeight()) / ui_scale;
        root_.setTransform(juce::AffineTransform::scale(ui_scale));
        root_.setBounds(0, 0, juce::roundToInt(logical_w), juce::roundToInt(logical_h));

        auto b = root_.getLocalBounds();
        header_.setBounds(b.removeFromTop(static_cast<int>(size::kHeaderHeight)));
        footer_.setBounds(b.removeFromBottom(static_cast<int>(size::kFooterHeight)));
        rail_.setBounds(b.removeFromRight(static_cast<int>(size::kRailWidth)));
        graph_.setBounds(b);
        const auto sheet_area = root_.getLocalBounds().withTrimmedTop(static_cast<int>(size::kHeaderHeight));
        preset_browser_.setBounds(sheet_area);
        settings_sheet_.setBounds(sheet_area);
        panel_was_visible_ = false;
        layoutPanel(0.0);

        if (!applying_size_ && settings_ != nullptr) {
            settings_->setValue("width", juce::roundToInt(logical_w));
            settings_->setValue("height", juce::roundToInt(logical_h));
        }
    }

    void Editor::visibilityChanged() {
        updateShowing();
    }

    void Editor::parentHierarchyChanged() {
        updateShowing();
    }

    void Editor::updateShowing() {
        const auto showing = isShowing();
        if (showing == is_showing_) {
            return;
        }
        is_showing_ = showing;
        p_ref_.getController().setEditorON(showing);
        if (showing) {
            spectrum_.start();
            last_timestamp_ = 0.0;
            // shortcuts should work as soon as the editor opens
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer(&graph_)] {
                if (safe != nullptr && safe->isShowing()) {
                    safe->grabKeyboardFocus();
                }
            });
            vblank_ = std::make_unique<juce::VBlankAttachment>(this, [this](const double ts) { frame(ts); });
        } else {
            vblank_.reset();
            spectrum_.stop();
        }
    }

    void Editor::frame(const double timestamp) {
        const auto dt = last_timestamp_ > 0.0 ? std::clamp(timestamp - last_timestamp_, 1e-4, .1) : 1.0 / 60.0;
        last_timestamp_ = timestamp;
        frame_rate_ += (1.0 / dt - frame_rate_) * .05;
        spectrum_.setRefreshRate(frame_rate_);

        bridge_.refresh();
        graph_.tick(dt);
        layoutPanel(dt);
        layoutMatchPanel(dt);
        header_.tick();
        if (bridge_.haveGlobalsChanged()) {
            footer_.tick();
        }
        rail_.tick(dt);
        preset_browser_.tick(static_cast<float>(dt));
        settings_sheet_.tick(static_cast<float>(dt));
        spectrum_.trigger();
        bridge_.clearChanges();
    }

    void Editor::runFrameForTesting(const double timestamp) {
        if (!is_showing_) {
            is_showing_ = true;
            p_ref_.getController().setEditorON(true);
            spectrum_.start();
        }
        frame(timestamp);
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
        for (auto* c = &component; c != nullptr && c != this; c = c->getParentComponent()) {
            if (const auto id = c->getComponentID(); id.isNotEmpty()) {
                if (auto* para = p_ref_.parameters_.getParameter(id)) {
                    return para->getParameterIndex();
                }
            }
        }
        return -1;
    }

    bool Editor::keyPressed(const juce::KeyPress& key) {
        const auto mods = key.getModifiers();
        if (mods.isCommandDown() && mods.isAltDown() && (key.getKeyCode() == 'F' || key.getKeyCode() == 'f')) {
            graph_.togglePerformanceOverlay();
            return true;
        }
        if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')) {
            mods.isShiftDown() ? graph_.redo() : graph_.undo();
            return true;
        }
        return false;
    }

    void Editor::setInterfaceScale(const float scale) {
        const auto old_scale = getScale();
        const auto logical_w = static_cast<float>(getWidth()) / old_scale;
        const auto logical_h = static_cast<float>(getHeight()) / old_scale;
        ui_settings_.scale = scale;
        applying_size_ = true;
        setResizeLimits(juce::roundToInt(size::kMinWidth * scale), juce::roundToInt(size::kMinHeight * scale),
                        juce::roundToInt(size::kMaxWidth * scale), juce::roundToInt(size::kMaxHeight * scale));
        setSize(juce::roundToInt(logical_w * scale), juce::roundToInt(logical_h * scale));
        applying_size_ = false;
        resized();
    }

    void Editor::applySettings(const UISettings& settings) {
        const auto scale_changed = std::abs(settings.scale - ui_settings_.scale) > 1e-3f;
        const auto new_scale = settings.scale;
        ui_settings_ = settings;
        if (scale_changed) {
            ui_settings_.scale = getScale();
            setInterfaceScale(new_scale);
        }
        ui_settings_.apply();
        ui_settings_.save(*settings_);
        laf_.refreshAccent();
        graph_.setSpectrumGrabEnabled(ui_settings_.spectrum_grab);
        root_.repaint();
    }

    void Editor::resetWindowSize() {
        const auto scale = getScale();
        setSize(juce::roundToInt(static_cast<float>(size::kDefaultWidth) * scale),
                juce::roundToInt(static_cast<float>(size::kDefaultHeight) * scale));
    }
}
