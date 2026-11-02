// Copyright (C) 2026 - Altvane
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

/**
 * The schema version of a product's saved state: host sessions and preset files alike, since presets are processor
 * states too. Each product writes an integer on the root element of the state it saves, its own kStateVersion, and
 * reads it back when it loads one:
 * - no version (a state saved before versions existed) is version 0, whose layout is version 1's: it loads unchanged
 * - an older version is brought up to date by the product's migrations, one version at a time, then loads
 * - a newer version (saved by a later build) loads what this build knows of it: each parameter it has, by ID (IDs
 *   never change meaning, see each product's "parameter IDs never change" test), and whatever else the product reads
 *   by name (the Leveler's session part). Everything else is dropped, so the state saves again as this build's
 *   version, which a later build migrates like any other older state, without anything newer in it labelled older.
 *   Parameters it lacks keep their current values, as they do for an older state
 * A product raises kStateVersion only when a layout change needs older states rewritten, and adds the migration from
 * the previous version with it.
 */
namespace altvane::ui::state_schema {
    inline constexpr auto kAttribute = "schema_version";

    /**
     * rewrites a state saved at version `from` (1 or later) into version from + 1's layout, in place
     */
    struct Migration {
        int from;
        void (*apply)(juce::XmlElement& state);
    };

    /**
     * the version a state was saved at: 0 if it has none
     */
    int read(const juce::XmlElement& state);

    void write(juce::XmlElement& state, int version);

    /**
     * brings a state up to `current` with the migrations from its version on (in order of `from`), and returns the
     * version it was saved at; a newer state is left as it is, for the product to keep what it knows of it
     */
    int upgrade(juce::XmlElement& state, int current, std::span<const Migration> migrations);

    /**
     * a parameter tree from a newer state, cut down to the parameters `parameters` has: the same type, with only their
     * PARAM children
     */
    juce::ValueTree knownParameters(const juce::ValueTree& saved, const juce::AudioProcessorValueTreeState& parameters);

    /**
     * after replaceState(): each switch exactly on the value the state gave it. A switch keeps whatever a host wrote to
     * it (0.4 is off), unlike every other parameter type, which keeps its plain value; and replaceState() leaves a
     * parameter alone when its plain value is already the saved one. So a switch the host had set to 0.4 would still
     * report 0.4 once a state saying "off" was restored, rather than the 0 it was saved at.
     */
    void settleSwitches(const juce::AudioProcessor& owner);
}
