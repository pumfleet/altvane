// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ui/util/value_format.hpp"
#include "ui/util/freq_scale.hpp"

using namespace contour::ui;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinAbs;

TEST_CASE("frequency formatting", "[format]") {
    CHECK(format::freq(63.4) == "63.4 Hz");
    CHECK(format::freq(440.0) == "440 Hz");
    CHECK(format::freq(1250.0) == "1.25 kHz");
    CHECK(format::freq(12500.0) == "12.5 kHz");
}

TEST_CASE("gain formatting uses a true minus sign", "[format]") {
    CHECK(format::gain(3.5) == "+3.50 dB");
    CHECK(format::gain(-3.5) == format::kMinus + "3.50 dB");
    CHECK(format::gain(0.0) == "0.00 dB");
    CHECK(format::gain(-12.25) == format::kMinus + "12.3 dB");
}

TEST_CASE("frequency parsing", "[format]") {
    CHECK_THAT(*format::parseFreq("1.2k"), WithinRel(1200.0, 1e-9));
    CHECK_THAT(*format::parseFreq("1200"), WithinRel(1200.0, 1e-9));
    CHECK_THAT(*format::parseFreq("1.2 kHz"), WithinRel(1200.0, 1e-9));
    CHECK_THAT(*format::parseFreq("80 Hz"), WithinRel(80.0, 1e-9));
    CHECK_THAT(*format::parseFreq("1k2"), WithinRel(1200.0, 1e-9));
    CHECK_THAT(*format::parseFreq("A4"), WithinRel(440.0, 1e-9));
    CHECK_THAT(*format::parseFreq("c#3"), WithinRel(138.591, 1e-4));
    CHECK_THAT(*format::parseFreq("Eb2"), WithinRel(77.782, 1e-4));
    CHECK_FALSE(format::parseFreq("abc").has_value());
    CHECK_FALSE(format::parseFreq("").has_value());
    CHECK_FALSE(format::parseFreq("-5").has_value());
}

TEST_CASE("number parsing accepts hyphen, minus sign and units", "[format]") {
    CHECK_THAT(*format::parseNumber("-3"), WithinAbs(-3.0, 1e-9));
    CHECK_THAT(*format::parseNumber(format::kMinus + "3.5 dB"), WithinAbs(-3.5, 1e-9));
    CHECK_THAT(*format::parseNumber("+2,5"), WithinAbs(2.5, 1e-9));
    CHECK_FALSE(format::parseNumber("dB").has_value());
}

TEST_CASE("note names", "[format]") {
    CHECK(format::note(440.0) == "A4");
    CHECK(format::note(261.6256) == "C4");
    CHECK(format::note(450.0).startsWith("A4 +"));
}

TEST_CASE("frequency scale round trip", "[scale]") {
    FreqScale scale;
    scale.setBounds(0.f, 1000.f);
    CHECK_THAT(scale.freqToX(FreqScale::kMinFreq), WithinAbs(0.0, 1e-3));
    CHECK_THAT(scale.freqToX(FreqScale::kMaxFreq), WithinAbs(1000.0, 1e-2));
    for (const double f : {20.0, 100.0, 1000.0, 7500.0, 20000.0}) {
        CHECK_THAT(scale.xToFreq(scale.freqToX(f)), WithinRel(f, 1e-4));
    }
    GainScale gain;
    gain.setBounds(0.f, 400.f, 20.f);
    gain.setMaxDB(12.f);
    CHECK_THAT(gain.dbToY(0.f), WithinAbs(200.0, 1e-4));
    CHECK_THAT(gain.dbToY(12.f), WithinAbs(20.0, 1e-4));
    CHECK_THAT(gain.yToDB(gain.dbToY(-7.5f)), WithinAbs(-7.5, 1e-4));
}
