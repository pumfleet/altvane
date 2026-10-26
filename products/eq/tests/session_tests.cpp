// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.hpp"
#include "ui/model/engine_bridge.hpp"
#include "ui/model/band_ops.hpp"
#include "ui/model/session_ops.hpp"
#include "ui/model/preset_manager.hpp"
#include "ui/analysis/match_engine.hpp"
#include "ui/analysis/spectrum_worker.hpp"

using namespace contour::ui;
using FT = zldsp::filter::FilterType;

namespace {
    struct Fixture {
        PluginProcessor p;
        std::unique_ptr<EngineBridge> bridge;

        Fixture() {
            p.setRateAndBufferSizeDetails(48000.0, 512);
            p.prepareToPlay(48000.0, 512);
            bridge = std::make_unique<EngineBridge>(p);
        }

        size_t activeCount() {
            bridge->refresh();
            size_t n = 0;
            for (size_t b = 0; b < kBandNum; ++b) {
                n += bridge->band(b).isActive() ? 1 : 0;
            }
            return n;
        }
    };
}

TEST_CASE("copy and paste bands through the clipboard", "[session]") {
    Fixture f;
    auto& bridge = *f.bridge;
    const auto band = band_ops::add(bridge, 700.0, -3.5f, FT::kPeak);
    bridge.refresh();
    session_ops::copyBands(bridge, {band});
    REQUIRE(session_ops::canPaste());

    const auto pasted = session_ops::pasteBands(bridge);
    bridge.refresh();
    REQUIRE(pasted.size() == 1);
    CHECK(pasted[0] != band);
    CHECK(bridge.band(pasted[0]).isOn());
    CHECK(std::abs(bridge.band(pasted[0]).freq - 700.f) < .5f);
    CHECK(std::abs(bridge.band(pasted[0]).gain + 3.5f) < 1e-3f);

    // one paste is one undo step
    bridge.getUndoManager().undo();
    CHECK(f.activeCount() == 1);
}

TEST_CASE("duplicate shifts a third of an octave up", "[session]") {
    Fixture f;
    auto& bridge = *f.bridge;
    const auto band = band_ops::add(bridge, 1000.0, 2.f, FT::kPeak);
    bridge.refresh();
    const auto dup = session_ops::duplicateBands(bridge, {band});
    bridge.refresh();
    REQUIRE(dup.size() == 1);
    CHECK(std::abs(bridge.band(dup[0]).freq - 1259.92f) < 1.f);
    CHECK(std::abs(bridge.band(dup[0]).gain - 2.f) < 1e-3f);
}

TEST_CASE("A/B keeps two independent parameter states", "[session]") {
    Fixture f;
    auto& bridge = *f.bridge;
    const auto band = band_ops::add(bridge, 1000.0, 4.f, FT::kPeak);
    CHECK(session_ops::getABSlot(bridge) == 0);

    session_ops::switchAB(bridge); // B starts as a copy of A
    bridge.refresh();
    CHECK(session_ops::getABSlot(bridge) == 1);
    CHECK(std::abs(bridge.band(band).gain - 4.f) < 1e-3f);
    bridge.setNow({{bridge.params(band)[BandParams::kGain], -6.f}}, "B edit");

    session_ops::switchAB(bridge);
    bridge.refresh();
    CHECK(std::abs(bridge.band(band).gain - 4.f) < 1e-3f);

    session_ops::switchAB(bridge);
    bridge.refresh();
    CHECK(std::abs(bridge.band(band).gain + 6.f) < 1e-3f);
}

TEST_CASE("cancelling an edit restores values and leaves no undo step", "[session]") {
    Fixture f;
    auto& bridge = *f.bridge;
    const auto band = band_ops::add(bridge, 1000.0, 1.f, FT::kPeak);
    auto& um = bridge.getUndoManager();
    const auto steps_before = um.getNumActionsInCurrentTransaction() + (um.canUndo() ? 1 : 0);
    auto* gain = bridge.params(band)[BandParams::kGain];
    juce::RangedAudioParameter* paras[] = {gain};
    bridge.beginEdit(paras, "Drag");
    bridge.setPlain(gain, 9.f);
    bridge.cancelEdit();
    bridge.refresh();
    CHECK(std::abs(bridge.band(band).gain - 1.f) < 1e-3f);
    CHECK(um.getUndoDescription() == "Add Band");
    CHECK(steps_before == um.getNumActionsInCurrentTransaction() + (um.canUndo() ? 1 : 0));
}

TEST_CASE("preset manager tracks modifications", "[session]") {
    Fixture f;
    PresetManager presets(*f.bridge);
    CHECK_FALSE(presets.isModified());
    band_ops::add(*f.bridge, 300.0, 2.f, FT::kPeak);
    CHECK(presets.isModified());
    CHECK(presets.getCurrentName() == "Default");
}

TEST_CASE("EQ Match learns, fits and applies as one undo step", "[match]") {
    Fixture f;
    auto& bridge = *f.bridge;
    MatchEngine match(bridge);
    SpectrumWorker spectrum(f.p, &match);
    f.p.getController().setEditorON(true);
    spectrum.start();

    // a band that must be replaced by the match, and restored by cancel
    const auto existing = band_ops::add(bridge, 2000.0, 5.f, FT::kPeak);
    bridge.refresh();

    juce::Random random(7);
    juce::AudioBuffer<float> buffer(4, 512);
    juce::MidiBuffer midi;
    auto play = [&](const double seconds) {
        for (int i = 0; i < static_cast<int>(seconds * 48000.0 / 512.0); ++i) {
            for (int c = 0; c < 4; ++c) {
                for (int s = 0; s < 512; ++s) {
                    // bright noise: plenty for the natural reference to correct
                    buffer.setSample(c, s, (random.nextFloat() * 2.f - 1.f) * .2f);
                }
            }
            f.p.processBlock(buffer, midi);
            spectrum.trigger();
            juce::Thread::sleep(2);
        }
    };

    match.start();
    match.setTarget(MatchEngine::Target::kNatural);
    play(1.5);
    match.pullFrame();
    REQUIRE(match.getFrame().valid);

    auto run_until_result = [&] {
        for (int i = 0; i < 1200 && match.getState() != MatchEngine::State::kResult; ++i) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        }
    };

    match.fit();
    run_until_result();
    REQUIRE(match.getState() == MatchEngine::State::kResult);
    CHECK(match.getMaxBands() > 0);
    CHECK(match.getNumApplied() == match.getSuggested());
    bridge.refresh();
    CHECK(bridge.band(0).isOn());

    // cancel puts the original band back, with nothing to undo
    match.cancel();
    bridge.refresh();
    CHECK(bridge.band(existing).isOn());
    CHECK(std::abs(bridge.band(existing).freq - 2000.f) < 1.f);
    CHECK(bridge.getUndoManager().getUndoDescription() == "Add Band");

    // done keeps the match as a single step
    match.start();
    match.setTarget(MatchEngine::Target::kNatural);
    play(1.5);
    match.fit();
    run_until_result();
    REQUIRE(match.getState() == MatchEngine::State::kResult);
    match.finish();
    CHECK(bridge.getUndoManager().getUndoDescription() == "EQ Match");
    bridge.getUndoManager().undo();
    bridge.refresh();
    CHECK(std::abs(bridge.band(existing).freq - 2000.f) < 1.f);
    CHECK(std::abs(bridge.band(existing).gain - 5.f) < 1e-3f);

    spectrum.stop();
    f.p.getController().setEditorON(false);
}
