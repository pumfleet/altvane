// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "../PluginProcessor.hpp"
#include "factory_presets.hpp"
#include "theme/deesser_tokens.hpp"

namespace altvane::deesser {
    using namespace tokens;

    namespace {
        ui::ShellConfig makeConfig() {
            ui::ShellConfig config;
            config.product = "DE-ESSER";
            config.window = {
                size::kDefaultWidth, size::kDefaultHeight, size::kMinWidth, size::kMinHeight,
                size::kMaxWidth, size::kMaxHeight
            };
            config.preset_codec = ui::PresetCodec::xmlAsJson();
            config.credit = "AGPL-3.0";
            return config;
        }
    }

    Editor::Editor(PluginProcessor& p) :
        EditorShell(p, p.parameters_, p.session_, makeConfig()),
        p_ref_(p),
        history_(p, param_editor_),
        band_view_(p, param_editor_, analyzer_),
        deck_(p, param_editor_),
        footer_(p, param_editor_),
        rail_(p) {
        presets_.installFactoryPresets(factoryPresets(), kFactoryPresetsVersion);
        for (auto* c : std::initializer_list<juce::Component*>{&history_, &band_view_, &deck_, &footer_, &rail_}) {
            root_.addAndMakeVisible(c);
        }
        settings_sheet_.setCardSize({560, 420});
        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
        p_ref_.getEngine().setFeedsActive(false);
    }

    void Editor::layout(const juce::Rectangle<int> area) {
        auto b = area;
        footer_.setBounds(b.removeFromBottom(static_cast<int>(size::kFooterHeight)));
        rail_.setBounds(b.removeFromRight(size::kMeterRailWidth));
        auto lower = b.removeFromBottom(size::kLowerHeight);
        deck_.setBounds(lower.removeFromRight(size::kDeckWidth));
        band_view_.setBounds(lower);
        history_.setBounds(b);
    }

    void Editor::showingChanged(const bool showing) {
        auto& engine = p_ref_.getEngine();
        engine.setFeedsActive(showing);
        if (showing) {
            // what piled up while closed is stale
            engine.clearFeeds();
        } else {
            deck_.cancelLearn();
        }
    }

    void Editor::tick(const double dt) {
        auto& engine = p_ref_.getEngine();
        const auto new_spectrum = analyzer_.process(engine);
        const auto reduction = engine.takeReductionPeak();
        history_.tick(dt);
        band_view_.tick(dt, reduction, new_spectrum);
        deck_.tick(dt);
        footer_.tick();
        rail_.tick(dt, reduction);
    }

    int Editor::getControlParameterIndex(juce::Component& component) {
        if (&component == &history_) {
            return history_.getHoveredParameterIndex();
        }
        if (&component == &band_view_) {
            return band_view_.getHoveredParameterIndex();
        }
        return EditorShell::getControlParameterIndex(component);
    }

    bool Editor::keyPressed(const juce::KeyPress& key) {
        const auto plain = !key.getModifiers().isCommandDown() && !key.getModifiers().isAltDown();
        if (plain && (key.getTextCharacter() == 'l' || key.getTextCharacter() == 'L')) {
            deck_.toggleLearn();
            return true;
        }
        if (plain && (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')) {
            history_.setFrozen(!history_.isFrozen());
            return true;
        }
        if (key == juce::KeyPress::escapeKey && (deck_.isLearning() || history_.isFrozen())) {
            deck_.cancelLearn();
            history_.setFrozen(false);
            return true;
        }
        return EditorShell::keyPressed(key);
    }
}
