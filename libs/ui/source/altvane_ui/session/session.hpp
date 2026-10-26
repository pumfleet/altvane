// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

namespace altvane::ui {
    /**
     * editor state a product's processor keeps, so it survives closing and reopening the editor:
     * the edit history, the A/B comparison slots and the preset file last loaded or saved
     */
    struct Session {
        juce::UndoManager undo_manager{30000, 200};
        // A/B comparison of the parameter state only
        std::array<juce::ValueTree, 2> ab_states;
        int ab_slot{0};
        juce::File current_preset;

        int getABSlot() const { return std::clamp(ab_slot, 0, 1); }

        /**
         * store the current state in this slot and recall the other one; the history is cleared
         */
        void switchAB(juce::AudioProcessorValueTreeState& parameters) {
            const auto current = static_cast<size_t>(getABSlot());
            const auto other = 1 - current;
            ab_states[current] = parameters.copyState();
            if (!ab_states[other].isValid()) {
                ab_states[other] = ab_states[current].createCopy();
            }
            parameters.replaceState(ab_states[other].createCopy());
            ab_slot = static_cast<int>(other);
            undo_manager.clearUndoHistory();
        }

        void copyToOtherSlot(juce::AudioProcessorValueTreeState& parameters) {
            ab_states[static_cast<size_t>(1 - getABSlot())] = parameters.copyState();
        }
    };
}
