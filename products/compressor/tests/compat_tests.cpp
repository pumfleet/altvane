// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "test_helpers.hpp"
#include "state/preset_json.hpp"
#include "ui/factory_presets.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;

/*
 * The fixtures in tests/fixtures are states as ZL Compressor writes them:
 *
 * - zl_compressor_0.5.0_session.xml is the XML inside the state ZL Compressor 0.5.0 gives a host (the host stores it
 *   as JUCE's binary wrapper around this text). 0.5.0 named its trees ZLCompressorParaState, ZLCompressorParameters
 *   and ZLCompressorNAParameters, had no side-chain EQ switch (side_eq_bypass) and ten display settings; 0.6.0
 *   renamed the trees and added the rest. It was made from the processor's own state, whose parameter layout is the
 *   vendored engine's, with the parameter and display lists cut back to 0.5.0's (upstream commit 956253a,
 *   source/zlp/zlp_definitions.hpp and source/state/state_definitions.hpp) and the trees renamed as 0.5.0's
 *   getStateInformation named them.
 * - zl_compressor_0.6.1_preset.json is a preset file as ZL Compressor 0.6.1's preset browser saves it, written by
 *   source/state/preset_json.cpp, which is upstream's source/panel/preset_browser/preset_json.cpp unchanged apart from
 *   its namespace (values are strings, as upstream writes them).
 */
namespace {
    juce::File fixture(const char* name) {
        return juce::File(ALTVANE_COMPRESSOR_FIXTURES).getChildFile(name);
    }

    float display(PluginProcessor& p, const char* id) {
        return p.na_parameters_.getRawParameterValue(id)->load();
    }
}

TEST_CASE("a ZL Compressor 0.5.0 session loads", "[compat]") {
    const auto xml = juce::XmlDocument::parse(fixture("zl_compressor_0.5.0_session.xml"));
    REQUIRE(xml != nullptr);
    REQUIRE(xml->hasTagName(zlstate::schema::legacy::kProcessorState));
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(*xml, state);

    PluginProcessor p;
    // what 0.5.0 did not save goes back to its default
    set(p, zlp::PSideEQBypass::kID, 1.f);
    set(p.na_parameters_, zlstate::PAnalyzerMaxDB::kID, 2.f);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    CHECK(get(p, zlp::PCompDirection::kID) == 2.f);
    CHECK(get(p, zlp::PCompStyle::kID) == 1.f);
    CHECK_THAT(get(p, zlp::PThreshold::kID), WithinAbs(-27.5, 1e-3));
    CHECK_THAT(get(p, zlp::PRatio::kID), WithinAbs(4.5, 1e-3));
    CHECK_THAT(get(p, zlp::PKneeW::kID), WithinAbs(6.0, 1e-3));
    CHECK_THAT(get(p, zlp::PAttack::kID), WithinAbs(12.0, 1e-3));
    CHECK_THAT(get(p, zlp::PRelease::kID), WithinAbs(180.0, 1e-3));
    CHECK_THAT(get(p, zlp::PHold::kID), WithinAbs(25.0, 1e-3));
    CHECK_THAT(get(p, zlp::PRange::kID), WithinAbs(30.0, 1e-3));
    CHECK_THAT(get(p, zlp::POutGain::kID), WithinAbs(3.5, 1e-3));
    CHECK_THAT(get(p, zlp::PWet::kID), WithinAbs(85.0, 1e-3));
    CHECK_THAT(get(p, zlp::PLookAhead::kID), WithinAbs(3.0, 1e-3));
    CHECK(get(p, zlp::POversample::kID) == 1.f);
    CHECK(get(p, zlp::PRMSON::kID) == 1.f);
    CHECK(get(p, zlp::PFilterStatus::kID + juce::String(1)) == 2.f);
    CHECK(get(p, zlp::PFilterType::kID + juce::String(1)) == 4.f);
    CHECK_THAT(get(p, zlp::PFreq::kID + juce::String(1)), WithinAbs(110.0, .01));
    CHECK(get(p, zlp::PSideEQBypass::kID) == 0.f);
    CHECK(display(p, zlstate::PAnalyzerTimeLength::kID) == 2.f);
    CHECK(display(p, zlstate::PSideControlDisplay::kID) == 1.f);
    CHECK(display(p, zlstate::PAnalyzerMaxDB::kID) == static_cast<float>(zlstate::PAnalyzerMaxDB::kDefaultI));

    // and it plays: expanding a quiet tone turns it down
    prepare(p);
    const auto out = run(p, {sine(1000.0, -50.f)}, seconds(1.0));
    CHECK(peakDB(out[0], static_cast<size_t>(seconds(.5))) < -52.f);
}

TEST_CASE("a ZL Compressor 0.6.1 preset loads and keeps the session's display", "[compat]") {
    const auto file = fixture("zl_compressor_0.6.1_preset.json");
    REQUIRE(file.loadFileAsString().contains("\"ZLCompressorPreset\""));
    PluginProcessor p;
    set(p.na_parameters_, zlstate::PAnalyzerTimeLength::kID, 1.f);
    set(p.na_parameters_, zlstate::PComputerCurveDisplay::kID, 1.f);

    const auto codec = altvane::compressor::presetCodec(p);
    juce::MemoryBlock state;
    REQUIRE(codec.read(file, state).wasOk());
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(get(p, zlp::PCompStyle::kID) == 3.f);
    CHECK_THAT(get(p, zlp::PThreshold::kID), WithinAbs(-22.0, 1e-3));
    CHECK_THAT(get(p, zlp::PRatio::kID), WithinAbs(2.5, 1e-3));
    CHECK_THAT(get(p, zlp::POutGain::kID), WithinAbs(4.0, 1e-3));
    CHECK_THAT(get(p, zlp::PSideGain::kID), WithinAbs(3.0, 1e-3));
    CHECK(get(p, zlp::PSideEQBypass::kID) == 1.f);
    CHECK(get(p, zlp::PFilterStatus::kID + juce::String(0)) == 2.f);
    CHECK_THAT(get(p, zlp::PFreq::kID + juce::String(0)), WithinAbs(3000.0, .01));
    CHECK_THAT(get(p, zlp::PGain::kID + juce::String(0)), WithinAbs(4.0, 1e-3));
    CHECK_THAT(get(p, zlp::PQ::kID + juce::String(0)), WithinAbs(1.5, 1e-3));
    CHECK(get(p, zlp::PFilterStatus::kID + juce::String(1)) == 1.f);
    // the preset's display (time 3, knee display closed) is not the session's
    CHECK(display(p, zlstate::PAnalyzerTimeLength::kID) == 1.f);
    CHECK(display(p, zlstate::PComputerCurveDisplay::kID) == 1.f);

    // read as it is (a host restoring ZL Compressor's state, for one), the display comes along
    REQUIRE(zlstate::PresetJson::read(file, state).wasOk());
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(display(p, zlstate::PAnalyzerTimeLength::kID) == 3.f);
    CHECK(display(p, zlstate::PComputerCurveDisplay::kID) == 0.f);
}

TEST_CASE("presets are ZL Compressor preset files", "[compat]") {
    PluginProcessor a;
    set(a, zlp::PThreshold::kID, -18.f);
    set(a, zlp::PFreq::kID + juce::String(5), 450.f);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    const auto file = juce::File::createTempFile(".json");
    REQUIRE(altvane::compressor::presetCodec(a).write(file, state).wasOk());
    CHECK(file.loadFileAsString().contains("\"ZLCompressorPreset\""));

    PluginProcessor b;
    juce::MemoryBlock loaded;
    REQUIRE(altvane::compressor::presetCodec(b).read(file, loaded).wasOk());
    b.setStateInformation(loaded.getData(), static_cast<int>(loaded.getSize()));
    CHECK_THAT(get(b, zlp::PThreshold::kID), WithinAbs(-18.0, 1e-3));
    CHECK_THAT(get(b, zlp::PFreq::kID + juce::String(5)), WithinAbs(450.0, .01));
    file.deleteFile();
}

TEST_CASE("factory presets load, set what they name and keep the session's display", "[presets]") {
    PluginProcessor p;
    set(p.na_parameters_, zlstate::PAnalyzerTimeLength::kID, 3.f);
    set(p.na_parameters_, zlstate::PSideControlDisplay::kID, 1.f);
    const auto codec = altvane::compressor::presetCodec(p);
    const auto presets = altvane::compressor::factoryPresets(p);
    // the eight of docs/design/mock.html
    juce::StringArray names;
    for (const auto& [path, text] : presets) {
        names.add(path);
    }
    CHECK(names == juce::StringArray{
              "Factory/Drum Smash", "Factory/Drum Bus Glue", "Factory/Vocal Leveler", "Factory/Mix Bus Gentle",
              "Factory/Bass Control", "Factory/Parallel Smash", "Factory/Upward Inflate", "Factory/Gate-ish Expander"
          });

    std::vector<float> input;
    for (int64_t n = 0; n < seconds(2.1); ++n) {
        input.push_back(bursts()(n));
    }
    const auto input_peak = peakDB(input);
    for (const auto& [path, text] : presets) {
        INFO(path);
        const auto file = juce::File::createTempFile(".json");
        REQUIRE(file.replaceWithText(text));
        juce::MemoryBlock state;
        REQUIRE(codec.read(file, state).wasOk());
        file.deleteFile();
        // a few blocks of the previous preset, so each one loads into a running processor
        prepare(p);
        run(p, {bursts()}, seconds(.1));
        p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

        CHECK(display(p, zlstate::PAnalyzerTimeLength::kID) == 3.f);
        CHECK(display(p, zlstate::PSideControlDisplay::kID) == 1.f);
        // every preset detects on linked left / right
        CHECK(get(p, zlp::PSideStereoMode::kID) == 1.f);
        CHECK(get(p, zlp::PSideStereoLink::kID) == 100.f);
        // and plays sensibly: nothing undefined, nothing far louder than what came in
        const auto out = run(p, {bursts(), silence(), seconds(.1)}, seconds(2.0));
        CHECK(std::all_of(out[0].begin(), out[0].end(), [](const float x) { return std::isfinite(x); }));
        CHECK(peakDB(out[0]) < input_peak + 10.f);
    }

    const auto load = [&](const juce::String& name) {
        for (const auto& [path, text] : presets) {
            if (path.endsWith(name)) {
                const auto file = juce::File::createTempFile(".json");
                REQUIRE(file.replaceWithText(text));
                juce::MemoryBlock state;
                REQUIRE(codec.read(file, state).wasOk());
                file.deleteFile();
                p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
                return;
            }
        }
        FAIL("no preset " << name);
    };
    load("Parallel Smash");
    CHECK_THAT(get(p, zlp::PWet::kID), WithinAbs(45.0, 1e-3));
    CHECK(get(p, zlp::PFilterStatus::kID + juce::String(0)) == 0.f);
    load("Drum Bus Glue");
    CHECK(get(p, zlp::PCompStyle::kID) == 1.f);
    CHECK(get(p, zlp::PFilterStatus::kID + juce::String(0)) == 2.f);
    CHECK(get(p, zlp::PFilterType::kID + juce::String(0)) == static_cast<float>(zldsp::filter::FilterType::kHighPass));
    CHECK_THAT(get(p, zlp::PWet::kID), WithinAbs(100.0, 1e-3));
    load("Gate-ish Expander");
    CHECK(get(p, zlp::PCompDirection::kID) == static_cast<float>(zlp::PCompDirection::kExpand));
    CHECK_THAT(get(p, zlp::PRange::kID), WithinAbs(24.0, 1e-3));
    load("Upward Inflate");
    CHECK(get(p, zlp::PCompDirection::kID) == static_cast<float>(zlp::PCompDirection::kInflate));
}
