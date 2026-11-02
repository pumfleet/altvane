// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include <altvane_ui/session/preset_manager.hpp>
#include <altvane_ui/session/state_schema.hpp>

#include "PluginProcessor.hpp"
#include "ui/factory_presets.hpp"

using namespace altvane::deesser;
namespace schema = altvane::ui::state_schema;

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

    void setParam(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    float plain(PluginProcessor& p, const juce::String& id) {
        return p.parameters_.getRawParameterValue(id)->load();
    }

    void edit(PluginProcessor& p) {
        setParam(p, params::kThreshold, -31.f);
        setParam(p, params::kRange, 9.5f);
        setParam(p, params::kMode, 1.f);
        setParam(p, params::kShape, 1.f);
        setParam(p, params::kBandLow, 4200.f);
        setParam(p, params::kBandHigh, 9000.f);
        setParam(p, params::kStereoMode, 2.f);
    }

    void checkSameParameters(PluginProcessor& a, PluginProcessor& b) {
        for (const auto& id : params::allIDs()) {
            CAPTURE(id);
            CHECK(std::abs(a.parameters_.getParameter(id)->getValue() - b.parameters_.getParameter(id)->getValue()) < 1e-6f);
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
            CHECK(std::abs(para->getValue() - before) < 1e-6f);
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
    // the attribute's name and its first value are part of the format
    CHECK(xml->getIntAttribute("schema_version") == 1);
    CHECK(PluginProcessor::kStateVersion == 1);

    PluginProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    checkSameParameters(a, b);
    CHECK(stateOf(b) == state);
}

TEST_CASE("a state without a schema version, as 0.1.0 saved it, loads as it always has", "[state]") {
    // 0.1.0's layout: the parameter tree with the product's version, and no schema version
    const auto legacy = juce::parseXML(R"(<?xml version="1.0" encoding="UTF-8"?>
<Parameters version="0.1.0">
  <PARAM id="threshold" value="-28.5"/>
  <PARAM id="range" value="7.0"/>
  <PARAM id="mode" value="1.0"/>
  <PARAM id="band_low" value="5000.0"/>
  <PARAM id="output_gain" value="-1.5"/>
</Parameters>)");
    REQUIRE(legacy != nullptr);
    REQUIRE(schema::read(*legacy) == 0);
    PluginProcessor p;
    load(p, *legacy);
    CHECK(std::abs(plain(p, params::kThreshold) + 28.5f) < 1e-4f);
    CHECK(std::abs(plain(p, params::kRange) - 7.f) < 1e-4f);
    CHECK(plain(p, params::kMode) == 1.f);
    CHECK(std::abs(plain(p, params::kBandLow) - 5000.f) < 1e-2f);
    CHECK(std::abs(plain(p, params::kOutput) + 1.5f) < 1e-4f);

    // the same as loading what this build saves, less the version
    PluginProcessor a;
    edit(a);
    auto xml = xmlOf(stateOf(a));
    xml->removeAttribute(schema::kAttribute);
    PluginProcessor b;
    load(b, *xml);
    checkSameParameters(a, b);
    // and it saves again as the current version
    CHECK(schema::read(*xmlOf(stateOf(b))) == PluginProcessor::kStateVersion);
}

TEST_CASE("a state from a newer build loads the parameters this build knows, and saves as this build's version", "[state]") {
    PluginProcessor a;
    edit(a);
    auto xml = xmlOf(stateOf(a));
    schema::write(*xml, PluginProcessor::kStateVersion + 1);
    // what a later build might add: a parameter, a property and a part this build has never heard of
    auto* future_param = xml->createNewChildElement("PARAM");
    future_param->setAttribute("id", "future_param");
    future_param->setAttribute("value", 3.0);
    xml->setAttribute("future_property", "x");
    xml->createNewChildElement("FutureSection")->setAttribute("future_value", 1);

    PluginProcessor b;
    load(b, *xml);
    checkSameParameters(a, b);
    const auto saved = xmlOf(stateOf(b));
    CHECK(schema::read(*saved) == PluginProcessor::kStateVersion);
    CHECK_FALSE(saved->toString().contains("future"));
    // and nothing else changes
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
    const auto codec = altvane::ui::PresetCodec::xmlAsJson();
    for (const auto& [name, text] : factoryPresets()) {
        CAPTURE(name);
        const auto file = juce::File::createTempFile(".json");
        REQUIRE(file.replaceWithText(text));
        juce::MemoryBlock block;
        REQUIRE(codec.read(file, block).wasOk());
        file.deleteFile();
        CHECK(schema::read(*xmlOf(block)) == PluginProcessor::kStateVersion);
        PluginProcessor p;
        p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        CHECK(plain(p, params::kThreshold) < 0.f);
    }

    PluginProcessor a;
    edit(a);
    const auto file = juce::File::createTempFile(".json");
    REQUIRE(codec.write(file, stateOf(a)).wasOk());
    juce::MemoryBlock block;
    REQUIRE(codec.read(file, block).wasOk());
    file.deleteFile();
    CHECK(schema::read(*xmlOf(block)) == PluginProcessor::kStateVersion);
    PluginProcessor b;
    b.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    checkSameParameters(a, b);
}

TEST_CASE("migrations bring an older state up one version at a time", "[state]") {
    // the hook every product's setStateInformation runs: here with two made-up steps, to version 3
    static constexpr std::array<schema::Migration, 2> migrations{{
        {1, [](juce::XmlElement& s) { s.setAttribute("steps", s.getStringAttribute("steps") + "1"); }},
        {2, [](juce::XmlElement& s) { s.setAttribute("steps", s.getStringAttribute("steps") + "2"); }},
    }};
    const auto run = [&](const int saved) {
        juce::XmlElement state("State");
        if (saved > 0) {
            schema::write(state, saved);
        }
        CHECK(schema::upgrade(state, 3, migrations) == saved);
        return state.getStringAttribute("steps");
    };
    // no version is version 1's layout
    CHECK(run(0) == "12");
    CHECK(run(1) == "12");
    CHECK(run(2) == "2");
    CHECK(run(3) == "");
    // a newer state is left for the product to take what it knows of
    CHECK(run(4) == "");
}
