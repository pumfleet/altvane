// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "engine_bridge.hpp"

namespace altvane::eq {
    /**
     * preset files on disk (upstream ZL Equalizer JSON format), groups are sub-folders
     */
    class PresetManager final {
    public:
        struct Entry {
            juce::File file;
            juce::String name, group;
            bool favourite{false};
        };

        static constexpr auto kExtension = ".json";

        explicit PresetManager(EngineBridge& bridge);

        void setSettings(juce::PropertiesFile* settings) { settings_ = settings; }

        static juce::File getDirectory();

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

        juce::File getCurrentFile() const { return bridge_.getProcessor().current_preset_; }

        juce::String getCurrentName() const;

        /**
         * true once parameters differ from the loaded (or saved) preset
         */
        bool isModified() const;

    private:
        EngineBridge& bridge_;
        juce::PropertiesFile* settings_{nullptr};
        std::vector<float> reference_;

        void captureReference();

        juce::StringArray getFavourites() const;
    };
}
