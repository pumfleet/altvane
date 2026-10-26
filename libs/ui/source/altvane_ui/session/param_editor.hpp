// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <span>
#include <juce_audio_processors/juce_audio_processors.h>

namespace altvane::ui {
    class ValueControl;

    /**
     * every parameter edit an editor makes goes through here:
     * the host sees one gesture per parameter and the undo history gets one step per edit
     */
    class ParamEditor final {
    public:
        explicit ParamEditor(juce::UndoManager& undo_manager);

        ~ParamEditor();

        /**
         * begin a continuous edit (drag, wheel), the host sees one gesture per parameter
         * and the undo history gets a single transaction when the edit ends
         */
        void beginEdit(std::span<juce::RangedAudioParameter* const> paras, const juce::String& name);

        void setPlain(juce::RangedAudioParameter* para, float plain);

        void endEdit();

        /**
         * end the current edit and restore every parameter it touched, leaving no undo step
         */
        void cancelEdit();

        bool isEditing() const { return !edit_.entries.empty(); }

        /**
         * set several parameters at once as one undoable step
         */
        void setNow(std::initializer_list<std::pair<juce::RangedAudioParameter*, float>> values,
                    const juce::String& name);

        void setNow(const std::vector<std::pair<juce::RangedAudioParameter*, float>>& values,
                    const juce::String& name);

        /**
         * flip a boolean parameter as one undoable step
         */
        void toggle(juce::RangedAudioParameter* para, const juce::String& name);

        /**
         * set a parameter outside the undo history (non-automatable UI state)
         */
        static void setNotifyingHost(juce::RangedAudioParameter* para, float normalised);

        void undo();

        void redo();

        juce::UndoManager& getUndoManager() { return undo_manager_; }

    private:
        struct EditEntry {
            juce::RangedAudioParameter* para;
            float from;
        };

        struct Edit {
            juce::String name;
            std::vector<EditEntry> entries;
        };

        juce::UndoManager& undo_manager_;
        Edit edit_;
    };

    /**
     * wire a value control to a parameter: range, default, text entry, gestures and undo,
     * and the component ID the editor uses to open the host's parameter menu
     */
    void bindValue(ValueControl& control, ParamEditor& editor, juce::RangedAudioParameter* para,
                   const juce::String& name);
}
