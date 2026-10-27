// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "ui/analyzer.hpp"
#include "signals.hpp"

using namespace altvane::deesser;
using namespace signals;

TEST_CASE("the finder lands on the esses' frequency", "[analysis]") {
    const auto centre = GENERATE(4500.0, 7000.0, 9500.0);
    const auto rate = GENERATE(44100.0, 96000.0);
    CAPTURE(centre, rate);
    // vowels and esses in turn
    const auto ms = [rate](const double m) { return static_cast<int>(m * .001 * rate); };
    juce::AudioBuffer<float> input(1, ms(4000));
    input.clear();
    for (int i = 0; i < 10; ++i) {
        addVowel(input, 0, ms(i * 400), ms(220), rate, .5f);
        addEss(input, 0, ms(i * 400 + 240), ms(140), rate, .25f, centre, i + 1);
    }
    dsp::Engine engine;
    engine.setFeedsActive(true);
    dsp::Controls c;
    c.band_low = static_cast<float>(centre / 1.6);
    c.band_low = std::clamp(c.band_low, 2500.f, 8000.f);
    // drained as the editor does, a frame at a time
    Analyzer analyzer;
    runEngine(engine, c, input, rate, nullptr, 1024, [&] { analyzer.process(engine); });
    const auto finder = analyzer.getFinder();
    CHECK(finder.confidence > .5f);
    CHECK(std::abs(std::log2(finder.hz / centre)) < 1.0 / 6.0);
}

TEST_CASE("the spectra read a tone at its level", "[analysis]") {
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> input(1, 48000);
    input.clear();
    addSine(input, 0, 0, input.getNumSamples(), rate, 1000.0, .5f);
    dsp::Engine engine;
    engine.setFeedsActive(true);
    dsp::Controls c;
    c.threshold_db = 0.f;
    runEngine(engine, c, input, rate);
    Analyzer analyzer;
    CHECK(analyzer.process(engine));
    float loudest = Analyzer::kSilenceDb;
    float at = 0.f;
    for (int p = 0; p < Analyzer::kPoints; ++p) {
        if (analyzer.getPre()[static_cast<size_t>(p)] > loudest) {
            loudest = analyzer.getPre()[static_cast<size_t>(p)];
            at = Analyzer::pointHz(p);
        }
    }
    CHECK(std::abs(loudest - juce::Decibels::gainToDecibels(.5f)) < 1.f);
    CHECK(std::abs(std::log2(at / 1000.f)) < .05f);
    // nothing sibilant was heard
    CHECK(analyzer.getFinder().confidence == 0.f);
}
