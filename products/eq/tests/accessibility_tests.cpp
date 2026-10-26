// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>

#include "altvane_ui/util/value_format.hpp"
#include "altvane_ui/widgets/widgets.hpp"
#include "PluginProcessor.hpp"
#include "ui/editor.hpp"
#include "ui/model/band_ops.hpp"

using namespace altvane::eq;

TEST_CASE("knobs expose formatted, settable values to assistive technology", "[accessibility]") {
    Knob knob;
    knob.setLabel("Freq");
    knob.setRange(zlp::PFreq::kRange, 1000.f);
    knob.format = [](const float v) { return format::freq(v); };
    knob.parse = [](const juce::String& t) -> std::optional<float> {
        if (const auto v = format::parseFreq(t)) {
            return static_cast<float>(*v);
        }
        return std::nullopt;
    };
    float changed = 0.f;
    int gestures = 0;
    knob.on_begin = [&] { ++gestures; };
    knob.on_change = [&](const float v) { changed = v; };
    knob.setValue(1250.f);

    // handlers are only handed out for components in a window, so build one directly
    const auto handler = knob.createAccessibilityHandler();
    REQUIRE(handler != nullptr);
    CHECK(handler->getRole() == juce::AccessibilityRole::slider);
    auto* value = handler->getValueInterface();
    REQUIRE(value != nullptr);
    CHECK(value->getCurrentValueAsString() == "1.25 kHz");

    value->setValueAsString("1.2k");
    CHECK(std::abs(changed - 1200.f) < .5f);
    CHECK(gestures == 1);
}

TEST_CASE("toggle chips report their state", "[accessibility]") {
    Chip chip("Auto Gain");
    chip.setOn(true);
    const auto handler = chip.createAccessibilityHandler();
    REQUIRE(handler != nullptr);
    CHECK(handler->getRole() == juce::AccessibilityRole::toggleButton);
    CHECK(handler->getCurrentState().isChecked());
}

TEST_CASE("controls map to host parameters", "[accessibility]") {
    PluginProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 512);
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor = dynamic_cast<Editor*>(base.get());
    REQUIRE(editor != nullptr);

    EngineBridge bridge(p);
    const auto band = band_ops::add(bridge, 500.0, 3.f, zldsp::filter::kPeak);
    editor->runFrameForTesting(1.0);
    editor->getSelectionForTesting().select(band);
    editor->runFrameForTesting(1.02);

    // over the graph the host sees the selected band's frequency
    const auto idx = editor->getControlParameterIndex(editor->getGraphForTesting());
    CHECK(idx == p.parameters_.getParameter(zlp::PFreq::kID + juce::String(band))->getParameterIndex());
    base.reset();
}
