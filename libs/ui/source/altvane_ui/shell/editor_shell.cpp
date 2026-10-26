// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "editor_shell.hpp"

#include "../theme/tokens.hpp"

namespace altvane::ui {
    using namespace tokens;

    EditorShell::EditorShell(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& parameters,
                             Session& session, ShellConfig config) :
        AudioProcessorEditor(&p),
        param_editor_(session.undo_manager),
        presets_(p, param_editor_, session, config.preset_codec),
        header_(config.product, param_editor_, session, parameters, presets_),
        preset_browser_(presets_),
        processor_(p),
        parameters_(parameters),
        config_(std::move(config)) {
        settings_ = UISettings::openFile();
        for (const auto& s : config_.switches) {
            ui_settings_.switches[s.key] = s.default_value;
            settings_sheet_.addSwitch(s.section, s.label, s.key);
        }
        ui_settings_.load(*settings_);
        ui_settings_.apply();
        laf_.refreshAccent();
        presets_.setSettings(settings_.get());
        settings_sheet_.setCredit(config_.credit);
        setLookAndFeel(&laf_);
        tooltip_window_.setLookAndFeel(&laf_);
        setOpaque(true);
        addAndMakeVisible(root_);
        root_.addAndMakeVisible(header_);

        const auto refocus = [this] { focusDefault(); };
        preset_browser_.on_closed = refocus;
        settings_sheet_.on_closed = refocus;
        settings_sheet_.on_change = [this](const UISettings& s) { applySettings(s); };
        settings_sheet_.on_reset_window_size = [this] { resetWindowSize(); };

        header_.on_undo = [this] { param_editor_.undo(); };
        header_.on_redo = [this] { param_editor_.redo(); };
        header_.on_open_presets = [this] {
            settings_sheet_.close();
            preset_browser_.open();
        };
        header_.on_open_settings = [this] {
            preset_browser_.close();
            settings_sheet_.setSettings(ui_settings_);
            settings_sheet_.open();
        };
    }

    EditorShell::~EditorShell() {
        stopFrames();
        if (settings_ != nullptr) {
            settings_->saveIfNeeded();
        }
        tooltip_window_.setLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }

    void EditorShell::finishSetup() {
        // sheets cover the product's components
        root_.addChildComponent(preset_browser_);
        root_.addChildComponent(settings_sheet_);

        const auto& w = config_.window;
        const auto ui_scale = getScale();
        const auto width = settings_->getIntValue("width", w.default_width);
        const auto height = settings_->getIntValue("height", w.default_height);

        setResizable(true, processor_.wrapperType != juce::AudioProcessor::wrapperType_AudioUnitv3);
        applying_size_ = true;
        setResizeLimits(juce::roundToInt(static_cast<float>(w.min_width) * ui_scale),
                        juce::roundToInt(static_cast<float>(w.min_height) * ui_scale),
                        juce::roundToInt(static_cast<float>(w.max_width) * ui_scale),
                        juce::roundToInt(static_cast<float>(w.max_height) * ui_scale));
        setSize(juce::roundToInt(static_cast<float>(std::clamp(width, w.min_width, w.max_width)) * ui_scale),
                juce::roundToInt(static_cast<float>(std::clamp(height, w.min_height, w.max_height)) * ui_scale));
        applying_size_ = false;
        resized();
        updateShowing();
    }

    void EditorShell::stopFrames() {
        vblank_.reset();
    }

    void EditorShell::paint(juce::Graphics& g) {
        g.fillAll(colour::kWindow);
    }

    void EditorShell::resized() {
        const auto ui_scale = getScale();
        const auto logical_w = static_cast<float>(getWidth()) / ui_scale;
        const auto logical_h = static_cast<float>(getHeight()) / ui_scale;
        root_.setTransform(juce::AffineTransform::scale(ui_scale));
        root_.setBounds(0, 0, juce::roundToInt(logical_w), juce::roundToInt(logical_h));

        auto b = root_.getLocalBounds();
        header_.setBounds(b.removeFromTop(static_cast<int>(size::kHeaderHeight)));
        preset_browser_.setBounds(b);
        settings_sheet_.setBounds(b);
        layout(b);

        if (!applying_size_ && settings_ != nullptr) {
            settings_->setValue("width", juce::roundToInt(logical_w));
            settings_->setValue("height", juce::roundToInt(logical_h));
        }
    }

    void EditorShell::visibilityChanged() {
        updateShowing();
    }

    void EditorShell::parentHierarchyChanged() {
        updateShowing();
    }

    void EditorShell::focusDefault() {
        if (auto* c = getDefaultFocus(); c != nullptr && c->isShowing()) {
            c->grabKeyboardFocus();
        }
    }

    void EditorShell::updateShowing() {
        const auto showing = isShowing();
        if (showing == is_showing_) {
            return;
        }
        is_showing_ = showing;
        showingChanged(showing);
        if (showing) {
            last_timestamp_ = 0.0;
            // shortcuts should work as soon as the editor opens
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer(this)] {
                if (safe != nullptr) {
                    safe->focusDefault();
                }
            });
            vblank_ = std::make_unique<juce::VBlankAttachment>(this, [this](const double ts) { frame(ts); });
        } else {
            vblank_.reset();
        }
    }

    void EditorShell::frame(const double timestamp) {
        const auto dt = last_timestamp_ > 0.0 ? std::clamp(timestamp - last_timestamp_, 1e-4, .1) : 1.0 / 60.0;
        last_timestamp_ = timestamp;
        frame_rate_ += (1.0 / dt - frame_rate_) * .05;

        tick(dt);
        header_.tick();
        preset_browser_.tick(static_cast<float>(dt));
        settings_sheet_.tick(static_cast<float>(dt));
    }

    void EditorShell::runFrameForTesting(const double timestamp) {
        if (!is_showing_) {
            is_showing_ = true;
            showingChanged(true);
        }
        frame(timestamp);
    }

    int EditorShell::getControlParameterIndex(juce::Component& component) {
        for (auto* c = &component; c != nullptr && c != this; c = c->getParentComponent()) {
            if (const auto id = c->getComponentID(); id.isNotEmpty()) {
                if (auto* para = parameters_.getParameter(id)) {
                    return para->getParameterIndex();
                }
            }
        }
        return -1;
    }

    bool EditorShell::keyPressed(const juce::KeyPress& key) {
        const auto mods = key.getModifiers();
        if (mods.isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')) {
            mods.isShiftDown() ? param_editor_.redo() : param_editor_.undo();
            return true;
        }
        return false;
    }

    void EditorShell::setInterfaceScale(const float scale) {
        const auto& w = config_.window;
        const auto old_scale = getScale();
        const auto logical_w = static_cast<float>(getWidth()) / old_scale;
        const auto logical_h = static_cast<float>(getHeight()) / old_scale;
        ui_settings_.scale = scale;
        applying_size_ = true;
        setResizeLimits(juce::roundToInt(static_cast<float>(w.min_width) * scale),
                        juce::roundToInt(static_cast<float>(w.min_height) * scale),
                        juce::roundToInt(static_cast<float>(w.max_width) * scale),
                        juce::roundToInt(static_cast<float>(w.max_height) * scale));
        setSize(juce::roundToInt(logical_w * scale), juce::roundToInt(logical_h * scale));
        applying_size_ = false;
        resized();
    }

    void EditorShell::applySettings(const UISettings& settings) {
        const auto scale_changed = std::abs(settings.scale - ui_settings_.scale) > 1e-3f;
        const auto old_scale = ui_settings_.scale;
        const auto new_scale = settings.scale;
        ui_settings_ = settings;
        if (scale_changed) {
            // keep the logical size, so the window grows or shrinks with the scale
            ui_settings_.scale = old_scale;
            setInterfaceScale(new_scale);
        }
        ui_settings_.apply();
        ui_settings_.save(*settings_);
        laf_.refreshAccent();
        settingsApplied();
        root_.repaint();
    }

    void EditorShell::resetWindowSize() {
        const auto scale = getScale();
        setSize(juce::roundToInt(static_cast<float>(config_.window.default_width) * scale),
                juce::roundToInt(static_cast<float>(config_.window.default_height) * scale));
    }
}
