// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "../PluginProcessor.hpp"
#include "../presets.hpp"
#include "factory_presets.hpp"
#include "theme/leveler_tokens.hpp"

namespace altvane::leveler {
    using namespace tokens;

    namespace {
        ui::ShellConfig makeConfig() {
            ui::ShellConfig config;
            config.product = "LEVELER";
            config.window = {
                size::kDefaultWidth, size::kDefaultHeight, size::kMinWidth, size::kMinHeight,
                size::kMaxWidth, size::kMaxHeight
            };
            config.preset_codec = presetCodec();
            config.credit = "AGPL-3.0";
            return config;
        }
    }

    Editor::Editor(PluginProcessor& p) :
        EditorShell(p, p.parameters_, p.session_, makeConfig()),
        p_ref_(p),
        history_(p, param_editor_),
        rider_(p, param_editor_),
        deck_(p, param_editor_),
        footer_(p, param_editor_),
        rail_(p) {
        presets_.installFactoryPresets(factoryPresets(), kFactoryPresetsVersion);
        for (auto* c : std::initializer_list<juce::Component*>{&history_, &rider_, &deck_, &footer_, &rail_}) {
            root_.addAndMakeVisible(c);
        }
        settings_sheet_.setCardSize({560, 420});
        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
        p_ref_.getEngine().setPublishing(false);
    }

    void Editor::layout(const juce::Rectangle<int> area) {
        auto b = area;
        footer_.setBounds(b.removeFromBottom(static_cast<int>(size::kFooterHeight)));
        deck_.setBounds(b.removeFromBottom(static_cast<int>(size::kDeckHeight)));
        rail_.setBounds(b.removeFromRight(static_cast<int>(size::kRailWidth)));
        rider_.setBounds(b.removeFromRight(static_cast<int>(size::kRiderWidth)));
        history_.setBounds(b);
    }

    void Editor::tick(const double dt) {
        history_.tick();
        rider_.tick();
        deck_.tick();
        footer_.tick();
        rail_.tick(dt);
    }

    void Editor::showingChanged(const bool showing) {
        // the engine only queues history while someone looks at it; start from a clean slate
        history_.clear();
        p_ref_.getEngine().setPublishing(showing);
    }

    int Editor::getControlParameterIndex(juce::Component& component) {
        if (&component == &history_) {
            if (const auto idx = history_.getHoveredParameterIndex(); idx >= 0) {
                return idx;
            }
        }
        if (&component == &rider_.getFader()) {
            return rider_.getFader().getHoveredParameter()->getParameterIndex();
        }
        return EditorShell::getControlParameterIndex(component);
    }
}
