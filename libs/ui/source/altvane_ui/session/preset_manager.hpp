// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

#include "param_editor.hpp"
#include "session.hpp"

namespace altvane::ui {
    /**
     * how a product turns its processor state into a preset file and back
     */
    struct PresetCodec {
        std::function<juce::Result(const juce::File&, juce::MemoryBlock&)> read;
        std::function<juce::Result(const juce::File&, const juce::MemoryBlock&)> write;

        /**
         * the processor state as an XML document, written as JSON
         */
        static PresetCodec xmlAsJson();

        /**
         * the text xmlAsJson() writes for a processor state, e.g. to generate factory presets
         */
        static juce::String toJsonText(const juce::XmlElement& state);
    };

    /**
     * preset files on disk, groups are sub-folders
     */
    class PresetManager final {
    public:
        struct Entry {
            juce::File file;
            juce::String name, group;
            bool favourite{false};
        };

        static constexpr auto kExtension = ".json";

        PresetManager(juce::AudioProcessor& processor, ParamEditor& editor, Session& session, PresetCodec codec);

        void setSettings(juce::PropertiesFile* settings) { settings_ = settings; }

        static juce::File getDirectory();

        /**
         * copy the product's factory presets into the preset folder once per version, never over user edits
         * @param presets relative path (e.g. "Factory/Interview") -> preset file contents
         */
        void installFactoryPresets(const std::vector<std::pair<juce::String, juce::String>>& presets, int version);

        std::vector<Entry> scan() const;

        juce::StringArray getGroups() const;

        bool load(const juce::File& file);

        bool save(const juce::File& file);

        void initialise();

        void step(int delta);

        bool rename(const juce::File& file, const juce::String& new_name);

        bool remove(const juce::File& file);

        juce::File createGroup(const juce::String& name) const;

        bool isFavourite(const juce::File& file) const;

        void setFavourite(const juce::File& file, bool favourite);

        juce::File getCurrentFile() const { return session_.current_preset; }

        juce::String getCurrentName() const;

        /**
         * true once parameters differ from the loaded (or saved) preset
         */
        bool isModified() const;

    private:
        juce::AudioProcessor& processor_;
        ParamEditor& editor_;
        Session& session_;
        PresetCodec codec_;
        juce::PropertiesFile* settings_{nullptr};
        std::vector<float> reference_;

        void captureReference();

        juce::StringArray getFavourites() const;
    };
}
