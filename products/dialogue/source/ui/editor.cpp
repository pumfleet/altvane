// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "../PluginProcessor.hpp"
#include "factory_presets.hpp"
#include "theme/dialogue_tokens.hpp"

namespace altvane::dialogue {
    using namespace tokens;

    namespace {
        ui::ShellConfig makeConfig() {
            ui::ShellConfig config;
            config.product = "DIALOGUE";
            config.window = {
                size::kDefaultWidth, size::kDefaultHeight, size::kMinWidth, size::kMinHeight,
                size::kMaxWidth, size::kMaxHeight
            };
            config.preset_codec = ui::PresetCodec::xmlAsJson();
            config.credit = juce::String::fromUTF8("Separation model: DPDFNet by Ceva (Apache-2.0)  \xc2\xb7  AGPL-3.0");
            return config;
        }
    }

    Editor::Editor(PluginProcessor& p) :
        EditorShell(p, p.parameters_, p.session_, makeConfig()),
        p_ref_(p),
        visualiser_(p, param_editor_),
        deck_(p, param_editor_),
        footer_(p, param_editor_),
        rail_(p) {
        presets_.installFactoryPresets(factoryPresets(), kFactoryPresetsVersion);
        root_.addAndMakeVisible(visualiser_);
        root_.addAndMakeVisible(deck_);
        root_.addAndMakeVisible(footer_);
        root_.addAndMakeVisible(rail_);
        deck_.on_focus = [this](const int stem) { visualiser_.setFocus(stem); };
        settings_sheet_.setCardSize({560, 420});
        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
    }

    void Editor::layout(const juce::Rectangle<int> area) {
        auto b = area;
        footer_.setBounds(b.removeFromBottom(static_cast<int>(size::kFooterHeight)));
        rail_.setBounds(b.removeFromRight(static_cast<int>(size::kRailWidth)));
        deck_.setBounds(b.removeFromBottom(static_cast<int>(size::kDeckHeight)));
        visualiser_.setBounds(b);
    }

    void Editor::tick(const double dt) {
        visualiser_.tick(dt);
        deck_.tick(visualiser_.getFocus());
        footer_.tick();
        rail_.tick(dt);
        if (const auto late = p_ref_.getEngine().getUnderruns(); late != underruns_shown_) {
            underruns_shown_ = late;
            settings_sheet_.setStatus(late == 0 ? juce::String()
                                                : "Late model frames: " + juce::String(static_cast<int64_t>(late / 480))
                                                  + " (raise the buffer size or choose Eco)");
        }
    }

    int Editor::getControlParameterIndex(juce::Component& component) {
        if (&component == &visualiser_) {
            if (const auto idx = visualiser_.getHoveredParameterIndex(); idx >= 0) {
                return idx;
            }
        }
        return EditorShell::getControlParameterIndex(component);
    }

    bool Editor::keyPressed(const juce::KeyPress& key) {
        if (key == juce::KeyPress::escapeKey && visualiser_.getFocus() >= 0) {
            visualiser_.setFocus(Visualiser::kNoFocus);
            return true;
        }
        return EditorShell::keyPressed(key);
    }
}
