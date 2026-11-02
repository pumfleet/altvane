// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "test_helpers.hpp"
#include "state/preset_json.hpp"

using Catch::Matchers::WithinAbs;
using FT = zldsp::filter::FilterType;
using namespace test;

namespace {
    juce::File fixture(const char* name) {
        return juce::File(ALTVANE_EQ_FIXTURES).getChildFile(name);
    }

    float plain(const juce::RangedAudioParameter& para) {
        return para.convertFrom0to1(para.getValue());
    }

    /**
     * every parameter of an owner (the processor, or the dummy that holds the non-automatable ones) in a tree
     */
    std::vector<juce::RangedAudioParameter*> parametersOf(const juce::AudioProcessor& owner) {
        std::vector<juce::RangedAudioParameter*> result;
        for (auto* para : owner.getParameters()) {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(para)) {
                result.push_back(ranged);
            }
        }
        return result;
    }

    /**
     * moves every parameter away from its default, as a session with everything edited would
     */
    void dirty(const juce::AudioProcessor& owner) {
        for (auto* para : parametersOf(owner)) {
            para->setValueNotifyingHost(para->getDefaultValue() > .5f ? 0.f : 1.f);
        }
    }

    /**
     * each parameter the saved tree holds has its saved value; each it lacks is at its default
     */
    void checkLoaded(const juce::AudioProcessor& owner, const juce::ValueTree& saved) {
        REQUIRE(saved.isValid());
        for (auto* para : parametersOf(owner)) {
            const auto& id = para->getParameterID();
            const auto child = saved.getChildWithProperty("id", id);
            const auto expected = child.isValid()
                ? static_cast<float>(child.getProperty("value"))
                : para->convertFrom0to1(para->getDefaultValue());
            INFO(id << (child.isValid() ? " (saved)" : " (not saved, so its default)"));
            const auto tolerance = 1e-4 * std::max(1.0, std::abs(static_cast<double>(expected)));
            CHECK_THAT(plain(*para), WithinAbs(expected, tolerance));
        }
    }
}

TEST_CASE("parameter IDs never change", "[state]") {
    // saved sessions, presets and automation refer to these, as ZL Equalizer 2's do; this list may only grow
    const juce::StringArray global{
        "total_filter_structure", "total_external_side", "total_bypass", "total_output_gain", "total_gain_scale",
        "total_auto_gain", "total_static_gain", "total_phase_flip", "total_lookahead", "total_output_stereo"
    };
    const juce::StringArray per_band{
        "filter_status", "filter_type", "order", "lr_mode", "freq", "gain", "target_gain", "q", "dynamic_on",
        "dynamic_learn", "dynamic_bypass", "dynamic_relative", "side_swap", "side_link", "threshold", "knee_width",
        "attack", "release", "dynamic_rms_length", "dynamic_rms_mix", "dynamic_smooth", "side_filter_type",
        "side_order", "side_freq", "side_q"
    };
    const juce::StringArray non_automatable{
        "eq_max_db", "fft_top_db", "fft_min_db", "fft_pre_on", "fft_post_on", "fft_side_on", "fft_smooth_oct_value",
        "fft_smooth_erb_value", "fft_smooth_type", "fft_speed", "fft_tilt", "fft_freeze_on", "fft_stereo",
        "collision_on", "collision_strength", "piano_roll_on", "value_display_on", "meter_display_on"
    };
    auto expected = global;
    for (size_t b = 0; b < zlp::kBandNum; ++b) {
        for (const auto& id : per_band) {
            expected.add(id + juce::String(b));
        }
    }

    PluginProcessor p;
    // the order is the hosts' parameter index
    const auto ids = [](const juce::AudioProcessor& owner) {
        juce::StringArray result;
        for (const auto* para : parametersOf(owner)) {
            result.add(para->getParameterID());
        }
        return result;
    };
    CHECK(ids(p) == expected);
    CHECK(ids(p.dummy_processor_) == non_automatable);
}

TEST_CASE("state round trips, in ZL Equalizer 2's format", "[state]") {
    PluginProcessor a;
    set(a, zlp::PFilterStructure::kID, static_cast<float>(zlp::kParallel));
    set(a, zlp::POutputGain::kID, -2.5f);
    set(a, zlp::POutputStereo::kID, static_cast<float>(zlp::kSide));
    set(a, zlp::PLookahead::kID, 4.f);
    setBand(a, 3, zlp::PFilterStatus::kID, static_cast<float>(zlp::kOn));
    setBand(a, 3, zlp::PFilterType::kID, static_cast<float>(FT::kHighShelf));
    setBand(a, 3, zlp::PFreq::kID, 7200.f);
    setBand(a, 3, zlp::PGain::kID, -3.75f);
    setBand(a, 3, zlp::PLRMode::kID, static_cast<float>(zlp::kMid));
    setBand(a, 5, zlp::PFilterStatus::kID, static_cast<float>(zlp::kBypass));
    setBand(a, 5, zlp::PDynamicON::kID, 1.f);
    setBand(a, 5, zlp::PThreshold::kID, -27.f);
    set(a.parameters_NA_, zlstate::PEQMaxDB::kID, 2.f);
    set(a.parameters_NA_, zlstate::PPianoRollON::kID, 1.f);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(xml != nullptr);
    CHECK(xml->hasTagName(zlstate::schema::kProcessorState));
    const auto tree = juce::ValueTree::fromXml(*xml);

    // into an instance with everything changed, so nothing passes for having stayed at its default
    PluginProcessor b;
    dirty(b);
    dirty(b.dummy_processor_);
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    checkLoaded(b, tree.getChildWithName(zlstate::schema::kParameterState));
    checkLoaded(b.dummy_processor_, tree.getChildWithName(zlstate::schema::kNonAutomatableState));
    CHECK_THAT(plain(*b.parameters_.getParameter(zlp::PGain::kID + juce::String(3))), WithinAbs(-3.75, 1e-4));
}

TEST_CASE("presets are ZL Equalizer 2 preset files", "[state]") {
    PluginProcessor a;
    setBand(a, 1, zlp::PFilterStatus::kID, static_cast<float>(zlp::kOn));
    setBand(a, 1, zlp::PGain::kID, 4.5f);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    const auto file = juce::File::createTempFile(".json");
    REQUIRE(zlstate::PresetJson::write(file, state).wasOk());
    CHECK(file.loadFileAsString().contains("\"ZLEqualizerPreset\""));

    juce::MemoryBlock loaded;
    REQUIRE(zlstate::PresetJson::read(file, loaded).wasOk());
    PluginProcessor b;
    b.setStateInformation(loaded.getData(), static_cast<int>(loaded.getSize()));
    CHECK_THAT(plain(*b.parameters_.getParameter(zlp::PGain::kID + juce::String(1))), WithinAbs(4.5, 1e-4));
    file.deleteFile();
}

/*
 * The fixtures were written by ZL Equalizer 2's own code at its release commits, not by hand: a throwaway generator
 * compiled upstream's zlp/zlp_definitions.hpp and state/state_definitions.hpp from 2208c01 (1.3.0) and a3874d8
 * (1.4.1), each in a namespace of its own and with 24 bands as upstream builds, into JUCE AudioProcessorValueTreeStates
 * named as each release's PluginProcessor names them, set the values below, and saved them the way that release
 * does: 1.3.0's getStateInformation tree as XML (a host stores it wrapped by copyXmlToBinary), and 1.4.1's through
 * its own panel/preset_browser/preset_json.cpp.
 * - zl_equalizer_2_1.3.0_session.xml: "ZLCompressorParaState" holding "ZLEqualizerParameters" and
 *   "ZLEqualizerNAParameters", the names every release up to 1.3.0 used. State Variable, -1.5 dB output, static gain
 *   compensation; a 36 dB/oct high-pass at 32 Hz, a low shelf, a mid cut, a bypassed peak, a dynamic peak at 6.8 kHz
 *   (relative threshold) and a side-channel high shelf
 * - zl_equalizer_2_1.4.1_preset.json: "ZLEqualizerPreset" version 1 holding "ParaState", "Para" and "State". Mixed
 *   Phase, +2 dB output, phase flip; a low-pass, a left-channel cut, Flat Gain, Tilt Shelf, a bypassed notch and a
 *   dynamic peak keyed through a high-pass
 * No release has Output Stereo (added upstream after 1.4.1) or the piano roll, value and meter display settings.
 */
TEST_CASE("a ZL Equalizer 2 1.3.0 session loads", "[state]") {
    const auto xml = juce::XmlDocument::parse(fixture("zl_equalizer_2_1.3.0_session.xml"));
    REQUIRE(xml != nullptr);
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(*xml, state);

    PluginProcessor p;
    dirty(p);
    dirty(p.dummy_processor_);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto tree = juce::ValueTree::fromXml(*xml);
    checkLoaded(p, tree.getChildWithName("ZLEqualizerParameters"));
    checkLoaded(p.dummy_processor_, tree.getChildWithName("ZLEqualizerNAParameters"));

    const auto value = [&](const juce::String& id) { return plain(*p.parameters_.getParameter(id)); };
    CHECK_THAT(value(zlp::PFilterStructure::kID), WithinAbs(static_cast<float>(zlp::kSVF), 1e-6));
    CHECK_THAT(value(zlp::POutputStereo::kID), WithinAbs(static_cast<float>(zlp::kStereo), 1e-6));
    CHECK_THAT(value("filter_type0"), WithinAbs(static_cast<float>(FT::kHighPass), 1e-6));
    CHECK_THAT(value("gain2"), WithinAbs(-3.5, 1e-4));
    CHECK_THAT(value("lr_mode2"), WithinAbs(static_cast<float>(zlp::kMid), 1e-6));
    CHECK_THAT(value("filter_status3"), WithinAbs(static_cast<float>(zlp::kBypass), 1e-6));
    CHECK_THAT(value("dynamic_on4"), WithinAbs(1.f, 1e-6));
    CHECK_THAT(value("target_gain4"), WithinAbs(-7.5, 1e-4));

    // and it plays: the high-pass at 32 Hz takes out 15 Hz (36 dB/oct, about 40 dB down)
    prepare(p);
    const auto out = run(p, sine(15.0, -6.f), 1.0);
    CHECK(peakDB(out[0], 24000) < -30.f);
}

TEST_CASE("a ZL Equalizer 2 1.4.1 preset loads", "[state]") {
    juce::MemoryBlock state;
    REQUIRE(zlstate::PresetJson::read(fixture("zl_equalizer_2_1.4.1_preset.json"), state).wasOk());

    PluginProcessor p;
    dirty(p);
    dirty(p.dummy_processor_);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    REQUIRE(xml != nullptr);
    const auto tree = juce::ValueTree::fromXml(*xml);
    checkLoaded(p, tree.getChildWithName(zlstate::schema::kParameterState));
    checkLoaded(p.dummy_processor_, tree.getChildWithName(zlstate::schema::kNonAutomatableState));

    const auto value = [&](const juce::String& id) { return plain(*p.parameters_.getParameter(id)); };
    CHECK_THAT(value(zlp::PFilterStructure::kID), WithinAbs(static_cast<float>(zlp::kMixed), 1e-6));
    CHECK_THAT(value(zlp::POutputStereo::kID), WithinAbs(static_cast<float>(zlp::kStereo), 1e-6));
    CHECK_THAT(value("filter_type2"), WithinAbs(static_cast<float>(FT::kFlatGain), 1e-6));
    CHECK_THAT(value("lr_mode1"), WithinAbs(static_cast<float>(zlp::kLeft), 1e-6));
    CHECK_THAT(value("dynamic_on6"), WithinAbs(1.f, 1e-6));
    CHECK_THAT(value(zlp::PPhaseFlip::kID), WithinAbs(1.f, 1e-6));
    CHECK_THAT(p.parameters_NA_.getParameter(zlstate::PPianoRollON::kID)->getValue(), WithinAbs(0.0, 1e-6));
}
