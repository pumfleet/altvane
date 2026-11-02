// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include <altvane_ui/session/state_schema.hpp>

#include "PluginProcessor.hpp"
#include "presets.hpp"
#include "ui/factory_presets.hpp"

using namespace altvane::leveler;
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
        setParam(p, params::kTarget, -14.5f);
        setParam(p, params::kSpeed, .3f);
        setParam(p, params::kGuard, .25f);
        setParam(p, params::kRangeMin, -9.f);
        setParam(p, params::kLookahead, 0.f);
        p.setMode(dsp::Mode::kRead);
        *p.getRide() = 3.25f;
    }

    void checkSameState(PluginProcessor& a, PluginProcessor& b) {
        for (const auto& id : params::allIDs()) {
            CAPTURE(id);
            CHECK(std::abs(a.parameters_.getParameter(id)->getValue() - b.parameters_.getParameter(id)->getValue()) < 1e-6f);
        }
        CHECK(a.getMode() == b.getMode());
        CHECK(a.getRide()->get() == b.getRide()->get());
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
    // the attribute's name and its first value are part of the format
    CHECK(xmlOf(state)->getIntAttribute("schema_version") == 1);
    CHECK(PluginProcessor::kStateVersion == 1);

    PluginProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    checkSameState(a, b);
    CHECK(stateOf(b) == state);
}

TEST_CASE("a state without a schema version, as 0.1.0 saved it, loads as it always has", "[state]") {
    // 0.1.0's layout: the parameter tree with the product's version, then the session part, and no schema version
    const auto legacy = juce::parseXML(R"(<?xml version="1.0" encoding="UTF-8"?>
<Parameters version="0.1.0">
  <PARAM id="target" value="-21.0"/>
  <PARAM id="guard" value="0.4000000059604645"/>
  <PARAM id="lookahead" value="0.0"/>
  <Session mode="2" ride="-2.5"/>
</Parameters>)");
    REQUIRE(legacy != nullptr);
    REQUIRE(schema::read(*legacy) == 0);
    PluginProcessor p;
    load(p, *legacy);
    CHECK(std::abs(plain(p, params::kTarget) + 21.f) < 1e-4f);
    CHECK(std::abs(plain(p, params::kGuard) - .4f) < 1e-4f);
    CHECK(plain(p, params::kLookahead) == 0.f);
    CHECK(p.getMode() == dsp::Mode::kRead);
    CHECK(std::abs(p.getRide()->get() + 2.5f) < 1e-4f);
    CHECK_FALSE(p.parameters_.state.getChildWithName("Session").isValid());

    // the same as loading what this build saves, less the version
    PluginProcessor a;
    edit(a);
    auto xml = xmlOf(stateOf(a));
    xml->removeAttribute(schema::kAttribute);
    PluginProcessor b;
    load(b, *xml);
    checkSameState(a, b);
    CHECK(schema::read(*xmlOf(stateOf(b))) == PluginProcessor::kStateVersion);
}

TEST_CASE("a state from a newer build loads the parameters and session this build knows, and saves as this build's version", "[state]") {
    PluginProcessor a;
    edit(a);
    auto xml = xmlOf(stateOf(a));
    schema::write(*xml, PluginProcessor::kStateVersion + 1);
    // what a later build might add: a parameter, a property, a part and a session setting this build has never heard of
    auto* future_param = xml->createNewChildElement("PARAM");
    future_param->setAttribute("id", "future_param");
    future_param->setAttribute("value", 3.0);
    xml->setAttribute("future_property", "x");
    xml->createNewChildElement("FutureSection")->setAttribute("future_value", 1);
    xml->getChildByName("Session")->setAttribute("future_setting", 1);

    PluginProcessor b;
    load(b, *xml);
    checkSameState(a, b);
    const auto saved = xmlOf(stateOf(b));
    CHECK(schema::read(*saved) == PluginProcessor::kStateVersion);
    CHECK_FALSE(saved->toString().contains("future"));
    CHECK(stateOf(b) == stateOf(a));
}

TEST_CASE("the state restores exactly after every parameter moves, as pluginval checks", "[state]") {
    PluginProcessor fresh;
    checkRestoresExactly(fresh, 1);

    PluginProcessor edited;
    edited.setMode(dsp::Mode::kRead);
    juce::Random random(2);
    for (auto* para : edited.getParameters()) {
        para->setValueNotifyingHost(random.nextFloat());
    }
    checkRestoresExactly(edited, 3);
}

TEST_CASE("factory and user presets carry the schema version and load", "[state]") {
    const auto codec = presetCodec();
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
        CHECK(plain(p, params::kTarget) < 0.f);
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
    for (const auto& id : params::allIDs()) {
        CAPTURE(id);
        CHECK(std::abs(a.parameters_.getParameter(id)->getValue() - b.parameters_.getParameter(id)->getValue()) < 1e-6f);
    }
}
