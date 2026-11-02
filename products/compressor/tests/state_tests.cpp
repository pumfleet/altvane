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

#include <altvane_ui/session/state_schema.hpp>

#include "test_helpers.hpp"
#include "state/preset_json.hpp"
#include "ui/factory_presets.hpp"

using Catch::Matchers::WithinAbs;
using namespace test;
namespace schema = altvane::ui::state_schema;

/*
 * The schema version on Altvane's state (see altvane_ui/session/state_schema.hpp). ZL Compressor's own states, which
 * have none, are compat_tests.cpp's fixtures.
 */
namespace {
    juce::MemoryBlock stateOf(juce::AudioProcessor& p) {
        juce::MemoryBlock block;
        p.getStateInformation(block);
        return block;
    }

    std::unique_ptr<juce::XmlElement> xmlOf(const juce::MemoryBlock& block) {
        auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
        REQUIRE(xml != nullptr);
        return xml;
    }

    void load(juce::AudioProcessor& p, const juce::XmlElement& xml) {
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(xml, block);
        p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    }

    void edit(PluginProcessor& p) {
        set(p, zlp::PCompStyle::kID, 2.f);
        set(p, zlp::PThreshold::kID, -22.5f);
        set(p, zlp::PRatio::kID, 3.f);
        set(p, zlp::PFreq::kID + juce::String(2), 450.f);
        set(p, zlp::PFilterStatus::kID + juce::String(2), 2.f);
        set(p.na_parameters_, zlstate::PAnalyzerTimeLength::kID, 2.f);
    }

    void checkSameParameters(PluginProcessor& a, PluginProcessor& b) {
        for (const auto& [from, to] : {
                 std::pair{&a.parameters_, &b.parameters_}, std::pair{&a.na_parameters_, &b.na_parameters_}
             }) {
            const auto& owner = from == &a.parameters_ ? static_cast<juce::AudioProcessor&>(a) : a.dummy_processor_;
            for (auto* para : owner.getParameters()) {
                const auto id = dynamic_cast<juce::RangedAudioParameter*>(para)->getParameterID();
                CAPTURE(id);
                CHECK_THAT(to->getParameter(id)->getValue(), WithinAbs(para->getValue(), 1e-6));
            }
        }
    }

    /**
     * what pluginval's "Plugin state restoration" does at strictness 10: each automatable parameter but bypass moved,
     * the saved state set back (which must restore it), and at the end the state read again must be the one that was
     * set, byte for byte
     */
    void checkRestoresExactly(juce::AudioProcessor& p, const int seed) {
        const auto original = stateOf(p);
        juce::Random random(seed);
        for (auto* para : p.getParameters()) {
            if (!para->isAutomatable() || para == p.getBypassParameter()) {
                continue;
            }
            CAPTURE(para->getName(64));
            const auto before = para->getValue();
            para->setValueNotifyingHost(random.nextFloat());
            p.setStateInformation(original.getData(), static_cast<int>(original.getSize()));
            CHECK_THAT(para->getValue(), WithinAbs(before, 1e-6));
        }
        const auto again = stateOf(p);
        CHECK(xmlOf(again)->toString() == xmlOf(original)->toString());
        CHECK(again == original);
    }
}

TEST_CASE("the state carries its schema version, and round trips", "[state]") {
    PluginProcessor a;
    edit(a);
    const auto state = stateOf(a);
    const auto xml = xmlOf(state);
    // on ZL Compressor's root; the attribute's name and its first value are part of the format
    CHECK(xml->hasTagName(zlstate::schema::kProcessorState));
    CHECK(xml->getIntAttribute("schema_version") == 1);
    CHECK(PluginProcessor::kStateVersion == 1);

    PluginProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    checkSameParameters(a, b);
    CHECK(stateOf(b) == state);
}

TEST_CASE("a state without a schema version, as 0.1.0 saved it, loads as it always has", "[state]") {
    // 0.1.0 saved this build's layout without the version (older ZL Compressor states: compat_tests.cpp)
    PluginProcessor a;
    edit(a);
    auto xml = xmlOf(stateOf(a));
    xml->removeAttribute(schema::kAttribute);
    REQUIRE(schema::read(*xml) == 0);
    PluginProcessor b;
    load(b, *xml);
    checkSameParameters(a, b);
    CHECK(schema::read(*xmlOf(stateOf(b))) == PluginProcessor::kStateVersion);
}

TEST_CASE("a state from a newer build loads the parameters this build knows, and saves as this build's version", "[state]") {
    PluginProcessor a;
    edit(a);
    auto xml = xmlOf(stateOf(a));
    schema::write(*xml, PluginProcessor::kStateVersion + 1);
    // what a later build might add: parameters, a property and a part this build has never heard of
    for (auto* tree : xml->getChildIterator()) {
        auto* future_param = tree->createNewChildElement("PARAM");
        future_param->setAttribute("id", "future_param");
        future_param->setAttribute("value", 3.0);
        tree->setAttribute("future_property", "x");
    }
    xml->setAttribute("future_property", "x");
    xml->createNewChildElement("FutureSection")->setAttribute("future_value", 1);

    PluginProcessor b;
    load(b, *xml);
    checkSameParameters(a, b);
    const auto saved = xmlOf(stateOf(b));
    CHECK(schema::read(*saved) == PluginProcessor::kStateVersion);
    CHECK_FALSE(saved->toString().contains("future"));
    CHECK(stateOf(b) == stateOf(a));
}

TEST_CASE("the state restores exactly after every parameter moves, as pluginval checks", "[state]") {
    PluginProcessor fresh;
    checkRestoresExactly(fresh, 1);

    PluginProcessor edited;
    juce::Random random(2);
    for (auto* para : edited.getParameters()) {
        para->setValueNotifyingHost(random.nextFloat());
    }
    checkRestoresExactly(edited, 3);
}

TEST_CASE("factory and user presets carry the schema version and load", "[state]") {
    PluginProcessor p;
    const auto codec = altvane::compressor::presetCodec(p);
    for (const auto& [name, text] : altvane::compressor::factoryPresets(p)) {
        CAPTURE(name);
        const auto file = juce::File::createTempFile(".json");
        REQUIRE(file.replaceWithText(text));
        juce::MemoryBlock block;
        REQUIRE(codec.read(file, block).wasOk());
        file.deleteFile();
        CHECK(schema::read(*xmlOf(block)) == PluginProcessor::kStateVersion);
        p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        CHECK(get(p, zlp::PSideStereoLink::kID) == 100.f);
    }

    PluginProcessor a;
    edit(a);
    const auto file = juce::File::createTempFile(".json");
    REQUIRE(codec.write(file, stateOf(a)).wasOk());
    juce::MemoryBlock block;
    REQUIRE(zlstate::PresetJson::read(file, block).wasOk());
    file.deleteFile();
    CHECK(schema::read(*xmlOf(block)) == PluginProcessor::kStateVersion);
    PluginProcessor b;
    b.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    checkSameParameters(a, b);
}
