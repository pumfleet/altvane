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

#include <altvane_ui/session/ui_settings.hpp>
#include <altvane_ui/theme/tokens.hpp>
#include <altvane_ui/util/spring.hpp>

#include "fake_processor.hpp"

using namespace altvane::ui;
using namespace altvane::ui::test;
using Catch::Matchers::WithinAbs;

TEST_CASE("a new session is in slot A with nothing to undo", "[session]") {
    const Session s;
    CHECK(s.getABSlot() == 0);
    CHECK_FALSE(s.ab_states[0].isValid());
    CHECK_FALSE(s.ab_states[1].isValid());
    CHECK(s.current_preset == juce::File());
    CHECK_FALSE(s.undo_manager.canUndo());
}

TEST_CASE("the first switch to B starts B as a copy of A", "[session][ab]") {
    FakeProcessor p;
    p.setPlain(ids::kGain, 4.f);
    p.setPlain(ids::kMode, 2.f);
    p.session_.switchAB(p.parameters_);

    CHECK(p.session_.getABSlot() == 1);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(4.0, 1e-4));
    CHECK(p.plain(ids::kMode) == 2.f);
    CHECK(p.session_.ab_states[0].isValid());
    CHECK(p.session_.ab_states[1].isValid());
}

TEST_CASE("switching A/B swaps the parameter states both ways", "[session][ab]") {
    FakeProcessor p;
    p.setPlain(ids::kGain, 4.f);
    p.session_.switchAB(p.parameters_);

    // B
    p.editor_.setNow({{p.para(ids::kGain), -8.f}, {p.para(ids::kFreq), 120.f}}, "Edit B");
    p.editor_.toggle(p.para(ids::kBypass), "Bypass");
    p.session_.switchAB(p.parameters_);

    // back in A
    CHECK(p.session_.getABSlot() == 0);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(4.0, 1e-4));
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(1000.0, 1e-2));
    CHECK(p.plain(ids::kBypass) == 0.f);

    p.setPlain(ids::kGain, 1.f);
    p.session_.switchAB(p.parameters_);
    CHECK(p.session_.getABSlot() == 1);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(-8.0, 1e-4));
    CHECK_THAT(p.plain(ids::kFreq), WithinAbs(120.0, 1e-2));
    CHECK(p.plain(ids::kBypass) == 1.f);

    // A kept the change made in it before the switch
    p.session_.switchAB(p.parameters_);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(1.0, 1e-4));
}

TEST_CASE("switching A/B clears the edit history", "[session][ab]") {
    FakeProcessor p;
    p.editor_.setNow({{p.para(ids::kGain), 3.f}}, "Edit A");
    REQUIRE(p.session_.undo_manager.canUndo());
    p.session_.switchAB(p.parameters_);
    CHECK_FALSE(p.session_.undo_manager.canUndo());
    CHECK_FALSE(p.session_.undo_manager.canRedo());

    // so undo can't carry A's edit into B
    p.editor_.undo();
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(3.0, 1e-4));
}

TEST_CASE("copying to the other slot overwrites it with the current state", "[session][ab]") {
    FakeProcessor p;
    p.session_.switchAB(p.parameters_);
    p.setPlain(ids::kGain, -2.f);
    p.session_.switchAB(p.parameters_);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));

    // in A: copy A over B, then B reads like A
    p.setPlain(ids::kGain, 7.f);
    p.session_.copyToOtherSlot(p.parameters_);
    CHECK(p.session_.getABSlot() == 0);
    p.session_.switchAB(p.parameters_);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(7.0, 1e-4));
}

TEST_CASE("copying to the other slot before any switch seeds it", "[session][ab]") {
    FakeProcessor p;
    p.setPlain(ids::kGain, 5.f);
    p.session_.copyToOtherSlot(p.parameters_);
    p.setPlain(ids::kGain, -5.f);
    p.session_.switchAB(p.parameters_);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(5.0, 1e-4));
    p.session_.switchAB(p.parameters_);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(-5.0, 1e-4));
}

TEST_CASE("an out-of-range slot (e.g. from an old session) reads as A or B", "[session][ab]") {
    FakeProcessor p;
    p.session_.ab_slot = 7;
    CHECK(p.session_.getABSlot() == 1);
    p.session_.ab_slot = -3;
    CHECK(p.session_.getABSlot() == 0);

    p.session_.ab_slot = 7;
    p.setPlain(ids::kGain, 2.f);
    p.session_.switchAB(p.parameters_);
    CHECK(p.session_.ab_slot == 0);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(2.0, 1e-4));
}

TEST_CASE("the A/B slots hold copies, not the live state", "[session][ab]") {
    FakeProcessor p;
    p.session_.switchAB(p.parameters_);
    // parameter changes in B must not leak into the stored A (or the B snapshot) behind the session's back
    p.setPlain(ids::kGain, 9.f);
    p.session_.switchAB(p.parameters_);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(0.0, 1e-4));
    p.setPlain(ids::kGain, -9.f);
    p.session_.switchAB(p.parameters_);
    CHECK_THAT(p.plain(ids::kGain), WithinAbs(9.0, 1e-4));
}

// ---------------------------------------------------------------- UISettings

namespace {
    std::unique_ptr<juce::PropertiesFile> openSettings(const juce::File& file) {
        juce::PropertiesFile::Options options;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = -1;
        return std::make_unique<juce::PropertiesFile>(file, options);
    }
}

TEST_CASE("UI settings round-trip through their file", "[session][settings]") {
    const TempDir temp;
    const auto path = temp.get().getChildFile("settings.xml");
    {
        UISettings s;
        s.scale = 1.25f;
        s.accent = 2;
        s.reduce_motion = true;
        s.drag_sensitivity = .5f;
        s.wheel_sensitivity = 2.f;
        s.switches["show_meters"] = false;
        auto file = openSettings(path);
        s.save(*file);
        REQUIRE(file->save());
    }
    UISettings s;
    s.switches["show_meters"] = true;
    s.switches["not_saved"] = true;
    s.load(*openSettings(path));
    CHECK(s.scale == 1.25f);
    CHECK(s.accent == 2);
    CHECK(s.reduce_motion);
    CHECK(s.drag_sensitivity == .5f);
    CHECK(s.wheel_sensitivity == 2.f);
    CHECK_FALSE(s.getSwitch("show_meters"));
    // a switch the file doesn't have keeps its declared default; one never declared is off
    CHECK(s.getSwitch("not_saved"));
    CHECK_FALSE(s.getSwitch("never_declared"));
}

TEST_CASE("UI settings from a damaged or edited file are clamped", "[session][settings]") {
    const TempDir temp;
    auto file = openSettings(temp.get().getChildFile("settings.xml"));
    file->setValue("interface_scale", 40.0);
    file->setValue("accent", 99);
    file->setValue("drag_sensitivity", -1.0);
    file->setValue("wheel_sensitivity", 1000.0);

    UISettings s;
    s.load(*file);
    CHECK(s.scale == UISettings::kScales.back());
    CHECK(s.accent == static_cast<int>(tokens::colour::kAccentChoices.size()) - 1);
    CHECK(s.drag_sensitivity == .25f);
    CHECK(s.wheel_sensitivity == 4.f);

    file->setValue("interface_scale", 0.0);
    file->setValue("accent", -4);
    s.load(*file);
    CHECK(s.scale == UISettings::kScales.front());
    CHECK(s.accent == 0);
}

TEST_CASE("an empty settings file gives the defaults", "[session][settings]") {
    const TempDir temp;
    UISettings s;
    s.load(*openSettings(temp.get().getChildFile("settings.xml")));
    CHECK(s.scale == 1.f);
    CHECK(s.accent == 0);
    CHECK_FALSE(s.reduce_motion);
    CHECK(s.drag_sensitivity == 1.f);
    CHECK(s.wheel_sensitivity == 1.f);
}

TEST_CASE("applying UI settings sets the shared theme and interaction state", "[session][settings]") {
    const auto accent = tokens::colour::kAccent;
    UISettings s;
    s.accent = 1;
    s.reduce_motion = true;
    s.drag_sensitivity = 2.f;
    s.wheel_sensitivity = .5f;
    s.apply();
    CHECK(tokens::colour::kAccent == tokens::colour::kAccentChoices[1].second);
    CHECK(InteractionSettings::reduce_motion);
    CHECK(InteractionSettings::drag_sensitivity == 2.f);
    CHECK(InteractionSettings::wheel_sensitivity == .5f);

    UISettings{}.apply();
    CHECK_FALSE(InteractionSettings::reduce_motion);
    CHECK(InteractionSettings::drag_sensitivity == 1.f);
    tokens::colour::kAccent = accent;
}
