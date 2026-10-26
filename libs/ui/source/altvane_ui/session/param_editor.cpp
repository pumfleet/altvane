// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "param_editor.hpp"

#include "../util/value_format.hpp"
#include "../widgets/widgets.hpp"

namespace altvane::ui {
    namespace {
        // undo step: values are already applied when the action is first performed
        class ParameterAction final : public juce::UndoableAction {
        public:
            struct Entry {
                juce::RangedAudioParameter* para;
                float from, to;
            };

            explicit ParameterAction(std::vector<Entry> entries) : entries_(std::move(entries)) {
            }

            bool perform() override {
                apply(false);
                return true;
            }

            bool undo() override {
                apply(true);
                return true;
            }

            int getSizeInUnits() override { return static_cast<int>(entries_.size()) + 1; }

        private:
            std::vector<Entry> entries_;

            void apply(const bool backwards) {
                for (const auto& e : entries_) {
                    const auto v = backwards ? e.from : e.to;
                    if (std::abs(e.para->getValue() - v) > 1e-7f) {
                        ParamEditor::setNotifyingHost(e.para, v);
                    }
                }
            }
        };
    }

    ParamEditor::ParamEditor(juce::UndoManager& undo_manager) : undo_manager_(undo_manager) {
    }

    ParamEditor::~ParamEditor() {
        if (isEditing()) {
            endEdit();
        }
    }

    void ParamEditor::setNotifyingHost(juce::RangedAudioParameter* para, const float normalised) {
        para->beginChangeGesture();
        para->setValueNotifyingHost(normalised);
        para->endChangeGesture();
    }

    void ParamEditor::beginEdit(const std::span<juce::RangedAudioParameter* const> paras, const juce::String& name) {
        if (isEditing()) {
            endEdit();
        }
        edit_.name = name;
        for (auto* para : paras) {
            if (para == nullptr) {
                continue;
            }
            edit_.entries.push_back({para, para->getValue()});
            para->beginChangeGesture();
        }
    }

    void ParamEditor::setPlain(juce::RangedAudioParameter* para, const float plain) {
        if (para == nullptr) {
            return;
        }
        const auto& range = para->getNormalisableRange();
        const auto v = para->convertTo0to1(range.snapToLegalValue(std::clamp(plain, range.start, range.end)));
        if (std::abs(para->getValue() - v) > 1e-7f) {
            para->setValueNotifyingHost(v);
        }
    }

    void ParamEditor::endEdit() {
        std::vector<ParameterAction::Entry> changes;
        for (const auto& e : edit_.entries) {
            e.para->endChangeGesture();
            if (const auto to = e.para->getValue(); std::abs(to - e.from) > 1e-7f) {
                changes.push_back({e.para, e.from, to});
            }
        }
        if (!changes.empty()) {
            undo_manager_.beginNewTransaction(edit_.name);
            undo_manager_.perform(new ParameterAction(std::move(changes)), edit_.name);
        }
        edit_ = {};
    }

    void ParamEditor::cancelEdit() {
        for (const auto& e : edit_.entries) {
            if (std::abs(e.para->getValue() - e.from) > 1e-7f) {
                e.para->setValueNotifyingHost(e.from);
            }
            e.para->endChangeGesture();
        }
        edit_ = {};
    }

    void ParamEditor::setNow(const std::initializer_list<std::pair<juce::RangedAudioParameter*, float>> values,
                             const juce::String& name) {
        setNow(std::vector<std::pair<juce::RangedAudioParameter*, float>>(values), name);
    }

    void ParamEditor::setNow(const std::vector<std::pair<juce::RangedAudioParameter*, float>>& values,
                             const juce::String& name) {
        if (isEditing()) {
            endEdit();
        }
        std::vector<juce::RangedAudioParameter*> paras;
        paras.reserve(values.size());
        for (const auto& [para, v] : values) {
            paras.push_back(para);
        }
        beginEdit(paras, name);
        for (const auto& [para, v] : values) {
            setPlain(para, v);
        }
        endEdit();
    }

    void ParamEditor::toggle(juce::RangedAudioParameter* para, const juce::String& name) {
        if (para != nullptr) {
            setNow({{para, para->getValue() > .5f ? 0.f : 1.f}}, name);
        }
    }

    void ParamEditor::undo() {
        if (isEditing()) {
            endEdit();
        }
        undo_manager_.undo();
    }

    void ParamEditor::redo() {
        if (isEditing()) {
            endEdit();
        }
        undo_manager_.redo();
    }

    void bindValue(ValueControl& control, ParamEditor& editor, juce::RangedAudioParameter* para,
                   const juce::String& name) {
        control.setLabel(name);
        control.setComponentID(para->getParameterID());
        control.setRange(para->getNormalisableRange(), para->convertFrom0to1(para->getDefaultValue()));
        control.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = format::parseNumber(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        control.on_begin = [&editor, para, name] {
            juce::RangedAudioParameter* paras[] = {para};
            editor.beginEdit(paras, "Change " + name);
        };
        control.on_change = [&editor, para](const float v) { editor.setPlain(para, v); };
        control.on_end = [&editor] { editor.endEdit(); };
    }
}
