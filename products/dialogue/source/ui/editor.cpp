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

namespace altvane::dialogue {
    namespace {
        ui::ShellConfig makeConfig() {
            ui::ShellConfig config;
            config.product = "DIALOGUE";
            config.window = {900, 560, 680, 420};
            config.preset_codec = ui::PresetCodec::xmlAsJson();
            config.credit = juce::String(juce::CharPointer_UTF8("Separation by DPDFNet (Ceva, Apache-2.0)  \xc2\xb7  AGPL-3.0"));
            return config;
        }
    }

    Editor::Editor(PluginProcessor& p) :
        EditorShell(p, p.parameters_, p.session_, makeConfig()),
        p_ref_(p) {
        for (size_t s = 0; s < levels_.size(); ++s) {
            auto& knob = levels_[s];
            knob.setStyle(ui::Knob::Style::kHero);
            ui::bindValue(knob, param_editor_, p.parameters_.getParameter(params::kLevelIDs[s]), params::kStemNames[s]);
            knob.format = [](const float v) { return params::formatLevel(v); };
            root_.addAndMakeVisible(knob);
        }
        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
    }

    void Editor::layout(const juce::Rectangle<int> area) {
        auto b = area.reduced(40);
        const auto w = b.getWidth() / static_cast<int>(levels_.size());
        for (auto& knob : levels_) {
            knob.setBounds(b.removeFromLeft(w).withSizeKeepingCentre(160, 190));
        }
    }

    void Editor::tick(double) {
        for (size_t s = 0; s < levels_.size(); ++s) {
            levels_[s].setValue(p_ref_.parameters_.getRawParameterValue(params::kLevelIDs[s])->load());
        }
    }
}
