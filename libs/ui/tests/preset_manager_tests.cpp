// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdlib>
#include <cstring>

#include <altvane_ui/session/preset_manager.hpp>

#include "fake_processor.hpp"

#if JUCE_LINUX || JUCE_MAC
#include <sys/stat.h>
#endif

using namespace altvane::ui;
using namespace altvane::ui::test;
using Catch::Matchers::WithinAbs;

namespace {
    /**
     * where the tests' preset folders go: the temporary folder, except on Linux, where gio only trashes files on the
     * home folder's file system (often not /tmp's), so it's ~/.cache there
     */
    juce::File scratchRoot() {
#if JUCE_LINUX
        const auto cache = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".cache");
        cache.createDirectory();
        return cache;
#else
        return juce::File::getSpecialLocation(juce::File::tempDirectory);
#endif
    }

#if JUCE_LINUX
    /**
     * send removed presets to a trash of the run's own (gio reads XDG_DATA_HOME once per process),
     * so tests never fill the user's
     */
    void useTestTrash() {
        struct TestTrash {
            TempDir dir{scratchRoot()};

            TestTrash() { setenv("XDG_DATA_HOME", dir.get().getFullPathName().toRawUTF8(), 1); }
        };
        static TestTrash trash;
    }
#endif

    PresetCodec makeCodec() {
        auto codec = PresetCodec::xmlAsJson();
        codec.includes = [](const juce::AudioProcessorParameter& p) {
            const auto* with_id = dynamic_cast<const juce::AudioProcessorParameterWithID*>(&p);
            return with_id == nullptr || with_id->paramID != ids::kMeter;
        };
        return codec;
    }

    /**
     * a preset manager on a fake processor, its preset folder and settings file in a temporary folder
     */
    struct Presets {
        TempDir temp{scratchRoot()};
        FakeProcessor p;
        std::unique_ptr<juce::PropertiesFile> settings;
        PresetManager presets{p, p.editor_, p.session_, makeCodec()};

        explicit Presets(const bool with_settings = true) {
            PresetManager::setDirectoryOverride(temp.get().getChildFile("Presets"));
            if (with_settings) {
                juce::PropertiesFile::Options options;
                options.storageFormat = juce::PropertiesFile::storeAsXML;
                options.millisecondsBeforeSaving = -1;
                settings = std::make_unique<juce::PropertiesFile>(temp.get().getChildFile("settings.xml"), options);
                presets.setSettings(settings.get());
            }
#if JUCE_LINUX
            useTestTrash();
#endif
        }

        ~Presets() {
            PresetManager::setDirectoryOverride({});
        }

        static juce::File dir() { return PresetManager::getDirectory(); }

        juce::File file(const juce::String& relative) const { return dir().getChildFile(relative + PresetManager::kExtension); }

        /**
         * the preset file text for the fake processor with gain set, as a product's factory presets are written
         */
        static juce::String presetText(const float gain, const float mode = 0.f) {
            FakeProcessor other;
            other.setPlain(ids::kGain, gain);
            other.setPlain(ids::kMode, mode);
            return PresetCodec::toJsonText(*other.parameters_.copyState().createXml());
        }

        juce::StringArray names() const {
            juce::StringArray result;
            for (const auto& e : presets.scan()) {
                result.add(e.name);
            }
            return result;
        }

        juce::UndoManager& history() { return p.session_.undo_manager; }
    };
}

TEST_CASE("the preset folder can be moved for tests", "[presets]") {
    const auto normal = PresetManager::getDirectory();
    CHECK(normal.getFullPathName().contains("Altvane UI Tests"));
    {
        const Presets t;
        CHECK(Presets::dir() == t.temp.get().getChildFile("Presets"));
    }
    CHECK(PresetManager::getDirectory() == normal);
}

TEST_CASE("factory presets are installed into their groups", "[presets][factory]") {
    Presets t;
    t.presets.installFactoryPresets({{"Factory/Warm", Presets::presetText(3.f, 1.f)},
                                     {"Factory/Hot", Presets::presetText(9.f, 2.f)},
                                     {"Init", Presets::presetText(0.f)}}, 1);
    CHECK(t.file("Factory/Warm").existsAsFile());
    CHECK(t.file("Factory/Hot").existsAsFile());
    CHECK(t.file("Init").existsAsFile());
    CHECK(t.settings->getIntValue("factory_presets") == 1);

    const auto entries = t.presets.scan();
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].name == "Hot");
    CHECK(entries[0].group == "Factory");
    CHECK(entries[1].name == "Init");
    CHECK(entries[1].group.isEmpty());
    CHECK(entries[2].name == "Warm");
    CHECK(t.presets.getGroups() == juce::StringArray{"Factory"});

    REQUIRE(t.presets.load(t.file("Factory/Hot")));
    CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(9.0, 1e-4));
    CHECK(t.p.plain(ids::kMode) == 2.f);
    CHECK(t.presets.getCurrentName() == "Hot");
}

TEST_CASE("factory presets install once per version and never over the user's edits", "[presets][factory]") {
    Presets t;
    const std::vector<std::pair<juce::String, juce::String>> factory{
        {"Factory/Warm", Presets::presetText(3.f)}, {"Factory/Hot", Presets::presetText(9.f)}};
    t.presets.installFactoryPresets(factory, 1);

    // the user deletes one and overwrites the other
    REQUIRE(t.file("Factory/Warm").deleteFile());
    REQUIRE(t.file("Factory/Hot").replaceWithText(Presets::presetText(-1.f)));

    t.presets.installFactoryPresets(factory, 1);
    CHECK_FALSE(t.file("Factory/Warm").exists());

    // a newer version brings back what is missing but keeps the user's file
    t.presets.installFactoryPresets(factory, 2);
    CHECK(t.file("Factory/Warm").existsAsFile());
    CHECK(t.file("Factory/Hot").loadFileAsString() == Presets::presetText(-1.f));
    CHECK(t.settings->getIntValue("factory_presets") == 2);
}

TEST_CASE("without a settings file, factory presets fill in what is missing every time", "[presets][factory]") {
    Presets t(false);
    const std::vector<std::pair<juce::String, juce::String>> factory{{"Factory/Warm", Presets::presetText(3.f)}};
    t.presets.installFactoryPresets(factory, 1);
    REQUIRE(t.file("Factory/Warm").deleteFile());
    t.presets.installFactoryPresets(factory, 1);
    CHECK(t.file("Factory/Warm").existsAsFile());
    // and favourites are simply off
    t.presets.setFavourite(t.file("Factory/Warm"), true);
    CHECK_FALSE(t.presets.isFavourite(t.file("Factory/Warm")));
}

TEST_CASE("presets save and load the processor state", "[presets]") {
    Presets t;
    t.p.setPlain(ids::kGain, -7.5f);
    t.p.setPlain(ids::kFreq, 3200.f);
    t.p.setPlain(ids::kMode, 1.f);
    t.p.setPlain(ids::kBypass, 1.f);
    REQUIRE(t.presets.save(t.file("Mine/Vocal")));
    CHECK(t.file("Mine/Vocal").existsAsFile());
    CHECK(t.presets.getCurrentFile() == t.file("Mine/Vocal"));
    CHECK(t.presets.getCurrentName() == "Vocal");
    CHECK_FALSE(t.presets.isModified());

    t.presets.initialise();
    CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(0.0, 1e-4));

    REQUIRE(t.presets.load(t.file("Mine/Vocal")));
    CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(-7.5, 1e-4));
    CHECK_THAT(t.p.plain(ids::kFreq), WithinAbs(3200.0, 1e-1));
    CHECK(t.p.plain(ids::kMode) == 1.f);
    CHECK(t.p.plain(ids::kBypass) == 1.f);
    CHECK(t.presets.getCurrentFile() == t.file("Mine/Vocal"));
    CHECK_FALSE(t.presets.isModified());
}

TEST_CASE("a preset is modified once a preset parameter differs from it", "[presets]") {
    Presets t;
    REQUIRE(t.presets.save(t.file("Base")));
    CHECK_FALSE(t.presets.isModified());

    // a parameter left out of presets doesn't count
    t.p.setPlain(ids::kMeter, .9f);
    CHECK_FALSE(t.presets.isModified());

    t.p.setPlain(ids::kGain, 2.f);
    CHECK(t.presets.isModified());
    t.p.setPlain(ids::kGain, 0.f);
    CHECK_FALSE(t.presets.isModified());

    t.p.setPlain(ids::kGain, 2.f);
    REQUIRE(t.presets.save(t.file("Base")));
    CHECK_FALSE(t.presets.isModified());
}

TEST_CASE("loading a preset ends an open edit and clears the history", "[presets]") {
    Presets t;
    REQUIRE(t.presets.save(t.file("Base")));
    t.p.editor_.setNow({{t.p.para(ids::kGain), 5.f}}, "Edit");
    GestureCounter gain(t.p.para(ids::kGain));
    juce::RangedAudioParameter* paras[] = {t.p.para(ids::kGain)};
    t.p.editor_.beginEdit(paras, "Drag");
    t.p.editor_.setPlain(t.p.para(ids::kGain), 6.f);

    REQUIRE(t.presets.load(t.file("Base")));
    CHECK_FALSE(t.p.editor_.isEditing());
    CHECK(gain.begins == gain.ends);
    CHECK_FALSE(t.history().canUndo());
    CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
}

TEST_CASE("a corrupt or missing preset file loads nothing", "[presets][corrupt]") {
    Presets t;
    REQUIRE(t.presets.save(t.file("Good")));
    t.p.editor_.setNow({{t.p.para(ids::kGain), 4.f}}, "Edit");

    const std::vector<std::pair<const char*, const char*>> bad{
        {"Empty", ""},
        {"Truncated", "{\"tag\": \"FakeState\", \"children\": [{\"tag\": \"PARAM\", \"id\": \"ga"},
        {"Not JSON", "<FakeState><PARAM id=\"gain\" value=\"1\"/></FakeState>"},
        {"No tag", "{\"id\": \"gain\", \"value\": \"1\"}"},
        {"Array", "[1, 2, 3]"},
        {"Binary", "\x01\x02\xff\xfe garbage"},
    };
    for (const auto& [name, text] : bad) {
        INFO(name);
        const auto file = t.file(name);
        REQUIRE(file.replaceWithData(text, std::strlen(text)));
        CHECK_FALSE(t.presets.load(file));
        CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(4.0, 1e-4));
        CHECK(t.presets.getCurrentFile() == t.file("Good"));
        CHECK(t.history().canUndo());
    }
    CHECK_FALSE(t.presets.load(t.file("Does not exist")));
    CHECK(t.presets.getCurrentFile() == t.file("Good"));

    // corrupt files still list, so they can be deleted
    CHECK(t.names().contains("Truncated"));
}

TEST_CASE("saving over an existing preset replaces it", "[presets][collision]") {
    Presets t;
    t.p.setPlain(ids::kGain, 1.f);
    REQUIRE(t.presets.save(t.file("Same")));
    t.p.setPlain(ids::kGain, 2.f);
    REQUIRE(t.presets.save(t.file("Same")));
    CHECK(t.names() == juce::StringArray{"Same"});
    t.p.setPlain(ids::kGain, 0.f);
    REQUIRE(t.presets.load(t.file("Same")));
    CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(2.0, 1e-4));
}

TEST_CASE("renaming a preset moves its file, the current preset and its favourite", "[presets][rename]") {
    Presets t;
    REQUIRE(t.presets.save(t.file("Old")));
    t.presets.setFavourite(t.file("Old"), true);

    REQUIRE(t.presets.rename(t.file("Old"), "  New  "));
    CHECK_FALSE(t.file("Old").exists());
    CHECK(t.file("New").existsAsFile());
    CHECK(t.presets.getCurrentFile() == t.file("New"));
    CHECK(t.presets.isFavourite(t.file("New")));
    CHECK_FALSE(t.presets.isFavourite(t.file("Old")));
    CHECK(t.names() == juce::StringArray{"New"});
}

TEST_CASE("renaming onto another preset's name fails and keeps both", "[presets][rename][collision]") {
    Presets t;
    t.p.setPlain(ids::kGain, 1.f);
    REQUIRE(t.presets.save(t.file("One")));
    t.p.setPlain(ids::kGain, 2.f);
    REQUIRE(t.presets.save(t.file("Two")));
    const auto one = t.file("One").loadFileAsString();
    const auto two = t.file("Two").loadFileAsString();

    CHECK_FALSE(t.presets.rename(t.file("One"), "Two"));
    CHECK(t.file("One").loadFileAsString() == one);
    CHECK(t.file("Two").loadFileAsString() == two);
    CHECK(t.presets.getCurrentFile() == t.file("Two"));
}

TEST_CASE("renaming only the case of a preset's name works", "[presets][rename]") {
    // case-insensitive file systems (macOS, Windows) see the new name as the existing file
    Presets t;
    REQUIRE(t.presets.save(t.file("bass")));
    REQUIRE(t.presets.rename(t.file("bass"), "Bass"));
    CHECK(t.names() == juce::StringArray{"Bass"});
    CHECK(t.presets.getCurrentName() == "Bass");
}

TEST_CASE("renaming a preset to its own name succeeds and changes nothing", "[presets][rename]") {
    Presets t;
    REQUIRE(t.presets.save(t.file("Bass")));
    CHECK(t.presets.rename(t.file("Bass"), "Bass"));
    CHECK(t.names() == juce::StringArray{"Bass"});
    CHECK(t.presets.getCurrentName() == "Bass");
}

TEST_CASE("renaming to an empty or illegal name", "[presets][rename]") {
    Presets t;
    REQUIRE(t.presets.save(t.file("Keep")));
    CHECK_FALSE(t.presets.rename(t.file("Keep"), ""));
    CHECK_FALSE(t.presets.rename(t.file("Keep"), "   "));
    CHECK(t.file("Keep").existsAsFile());

    // characters a file name can't have are dropped rather than creating folders
    REQUIRE(t.presets.rename(t.file("Keep"), "a/b:c"));
    REQUIRE(t.presets.scan().size() == 1);
    CHECK(t.presets.scan()[0].group.isEmpty());
    CHECK(t.presets.getCurrentFile().existsAsFile());
}

TEST_CASE("removing a preset forgets it as current and favourite", "[presets][remove]") {
    Presets t;
    REQUIRE(t.presets.save(t.file("Doomed")));
    t.presets.setFavourite(t.file("Doomed"), true);
    REQUIRE(t.presets.save(t.file("Kept")));
    REQUIRE(t.presets.load(t.file("Doomed")));

    REQUIRE(t.presets.remove(t.file("Doomed")));
    CHECK_FALSE(t.file("Doomed").exists());
    CHECK(t.presets.getCurrentFile() == juce::File());
    CHECK(t.presets.getCurrentName() == "Default");
    CHECK(t.settings->getValue("favourite_presets").isEmpty());
    CHECK(t.names() == juce::StringArray{"Kept"});
}

TEST_CASE("a preset that can't be removed stays current and favourite", "[presets][remove]") {
#if JUCE_LINUX || JUCE_MAC
    Presets t;
    const auto locked = Presets::dir().getChildFile("Locked");
    REQUIRE(t.presets.save(t.file("Locked/Stuck")));
    t.presets.setFavourite(t.file("Locked/Stuck"), true);
    REQUIRE(chmod(locked.getFullPathName().toRawUTF8(), 0555) == 0);
    struct Unlock {
        juce::File dir;
        ~Unlock() { chmod(dir.getFullPathName().toRawUTF8(), 0755); }
    } unlock{locked};
    if (locked.getChildFile("probe").create().wasOk()) {
        SKIP("this user can write to a read-only folder (root?)");
    }

    CHECK_FALSE(t.presets.remove(t.file("Locked/Stuck")));
    CHECK(t.file("Locked/Stuck").existsAsFile());
    CHECK(t.presets.getCurrentFile() == t.file("Locked/Stuck"));
    CHECK(t.presets.isFavourite(t.file("Locked/Stuck")));
#else
    SKIP("needs POSIX permissions to make a folder read-only");
#endif
}

TEST_CASE("presets list in natural order with their groups and favourites", "[presets]") {
    Presets t;
    for (const auto* name : {"Preset 10", "Preset 2", "Drums/Kick", "Preset 1"}) {
        REQUIRE(t.presets.save(t.file(name)));
    }
    t.presets.setFavourite(t.file("Preset 2"), true);
    REQUIRE(t.file("Readme").withFileExtension("txt").replaceWithText("not a preset"));

    const auto entries = t.presets.scan();
    REQUIRE(entries.size() == 4);
    CHECK(entries[0].name == "Kick");
    CHECK(entries[0].group == "Drums");
    CHECK(entries[1].name == "Preset 1");
    CHECK(entries[2].name == "Preset 2");
    CHECK(entries[2].favourite);
    CHECK(entries[3].name == "Preset 10");
    CHECK_FALSE(entries[3].favourite);

    t.presets.setFavourite(t.file("Preset 2"), false);
    CHECK_FALSE(t.presets.isFavourite(t.file("Preset 2")));
}

TEST_CASE("groups are folders in the preset folder", "[presets]") {
    Presets t;
    const auto group = t.presets.createGroup("  Vocals  ");
    CHECK(group.isDirectory());
    CHECK(group == Presets::dir().getChildFile("Vocals"));
    t.presets.createGroup("Bass");
    CHECK(t.presets.getGroups() == juce::StringArray{"Bass", "Vocals"});
}

TEST_CASE("stepping through presets wraps around", "[presets][step]") {
    Presets t;
    for (const auto* name : {"A", "B", "C"}) {
        REQUIRE(t.presets.save(t.file(name)));
    }
    t.presets.initialise();
    REQUIRE(t.presets.getCurrentFile() == juce::File());

    // from no preset, forward goes to the first and back to the last
    t.presets.step(1);
    CHECK(t.presets.getCurrentName() == "A");
    t.presets.step(1);
    CHECK(t.presets.getCurrentName() == "B");
    t.presets.step(-1);
    CHECK(t.presets.getCurrentName() == "A");
    t.presets.step(-1);
    CHECK(t.presets.getCurrentName() == "C");
    t.presets.step(1);
    CHECK(t.presets.getCurrentName() == "A");

    t.presets.initialise();
    t.presets.step(-1);
    CHECK(t.presets.getCurrentName() == "C");
}

TEST_CASE("stepping with no presets does nothing", "[presets][step]") {
    Presets t;
    t.presets.step(1);
    CHECK(t.presets.getCurrentFile() == juce::File());
}

TEST_CASE("initialise resets preset parameters to their defaults as one undoable step", "[presets]") {
    Presets t;
    t.p.setPlain(ids::kGain, 5.f);
    t.p.setPlain(ids::kMode, 2.f);
    t.p.setPlain(ids::kMeter, .1f);
    REQUIRE(t.presets.save(t.file("Loud")));

    t.presets.initialise();
    CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
    CHECK(t.p.plain(ids::kMode) == 0.f);
    // left out of presets, so left alone
    CHECK_THAT(t.p.plain(ids::kMeter), WithinAbs(.1, 1e-6));
    CHECK(t.presets.getCurrentName() == "Default");
    CHECK_FALSE(t.presets.isModified());

    REQUIRE(t.history().canUndo());
    CHECK(t.history().getUndoDescription() == "Initialise");
    t.p.editor_.undo();
    CHECK_THAT(t.p.plain(ids::kGain), WithinAbs(5.0, 1e-4));
    CHECK(t.p.plain(ids::kMode) == 2.f);
}

TEST_CASE("the JSON codec round-trips a state", "[presets][codec]") {
    const TempDir temp;
    const auto codec = PresetCodec::xmlAsJson();
    FakeProcessor a;
    a.setPlain(ids::kGain, 3.25f);
    a.setPlain(ids::kBypass, 1.f);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    const auto file = temp.get().getChildFile("state.json");
    REQUIRE(codec.write(file, state).wasOk());
    CHECK(juce::JSON::parse(file).getProperty("tag", {}).toString() == "FakeState");

    juce::MemoryBlock read;
    REQUIRE(codec.read(file, read).wasOk());
    FakeProcessor b;
    b.setStateInformation(read.getData(), static_cast<int>(read.getSize()));
    CHECK_THAT(b.plain(ids::kGain), WithinAbs(3.25, 1e-4));
    CHECK(b.plain(ids::kBypass) == 1.f);

    // a state that isn't XML can't be written
    juce::MemoryBlock junk("junk", 4);
    CHECK_FALSE(codec.write(temp.get().getChildFile("junk.json"), junk).wasOk());
}
