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

#include <clocale>
#include <limits>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

#include <altvane_ui/util/value_format.hpp>

using namespace altvane::ui;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
    const juce::String kMinus = format::kMinus;

    /**
     * numbers as German writes them: 1.234,5
     */
    struct CommaDecimal final : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };

    /**
     * for the scope, a global C++ locale with a decimal comma (as a host may set), and the C locale too where the
     * system has one installed
     */
    class CommaLocale final {
    public:
        CommaLocale() : cpp_previous_(std::locale::global(std::locale(std::locale::classic(), new CommaDecimal))) {
            previous_ = std::setlocale(LC_ALL, nullptr);
            for (const auto* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252", "de-DE"}) {
                if (std::setlocale(LC_ALL, name) != nullptr) {
                    break;
                }
            }
        }

        ~CommaLocale() {
            std::setlocale(LC_ALL, previous_.c_str());
            std::locale::global(cpp_previous_);
        }

    private:
        std::locale cpp_previous_;
        std::string previous_;
    };
}

TEST_CASE("frequencies read in Hz below 1 kHz and kHz above", "[format]") {
    CHECK(format::freq(20.0) == "20.0 Hz");
    CHECK(format::freq(56.78) == "56.8 Hz");
    CHECK(format::freq(440.0) == "440 Hz");
    CHECK(format::freq(1234.0) == "1.23 kHz");
    CHECK(format::freq(12345.0) == "12.3 kHz");
    CHECK(format::freq(20000.0) == "20.0 kHz");
    CHECK(format::freq(1234.0, false) == "1.23");
    CHECK(format::freq(440.0, false) == "440");
}

TEST_CASE("frequencies that round up to a range boundary read as the next range", "[format]") {
    // as shown, never "100.0 Hz", "1000 Hz" or "10.00 kHz": the same frequency must not have two spellings
    CHECK(format::freq(99.97) == "100 Hz");
    CHECK(format::freq(999.7) == "1.00 kHz");
    CHECK(format::freq(9999.7) == "10.0 kHz");
}

TEST_CASE("gains have a sign, a typographic minus and fewer decimals from 10 dB", "[format]") {
    CHECK(format::gain(3.0) == "+3.00 dB");
    CHECK(format::gain(-3.0) == kMinus + "3.00 dB");
    CHECK(format::gain(-12.34) == kMinus + "12.3 dB");
    CHECK(format::gain(12.0, false) == "+12.0");
    CHECK(format::gain(3.0, true, false) == "3.00 dB");
    CHECK(format::gain(-3.0, true, false) == kMinus + "3.00 dB");
}

TEST_CASE("a gain that rounds to zero has no sign", "[format]") {
    CHECK(format::gain(0.0) == "0.00 dB");
    CHECK(format::gain(-0.0) == "0.00 dB");
    CHECK(format::gain(-0.004) == "0.00 dB");
    CHECK(format::gain(0.004) == "0.00 dB");
}

TEST_CASE("a gain that rounds up to 10 dB reads with the decimals of 10 dB", "[format]") {
    CHECK(format::gain(9.996) == "+10.0 dB");
    CHECK(format::gain(-9.996) == kMinus + "10.0 dB");
    CHECK(format::q(9.996) == "10.0");
}

TEST_CASE("an infinite gain reads as inf", "[format]") {
    const auto inf = std::numeric_limits<double>::infinity();
    CHECK(format::gain(-inf) == kMinus + "inf dB");
    CHECK(format::gain(inf) == "+inf dB");
}

TEST_CASE("times from 100 ms read in whole milliseconds, and from 1 s in seconds", "[format]") {
    CHECK(format::ms(250.4) == "250 ms");
    CHECK(format::ms(333.333) == "333 ms");
    // as shown: never "100.0 ms" or "1000 ms"
    CHECK(format::ms(99.97) == "100 ms");
    CHECK(format::ms(999.7) == "1.00 s");
}

TEST_CASE("q, times, percentages and notes", "[format]") {
    CHECK(format::q(0.707) == "0.71");
    CHECK(format::q(12.0) == "12.0");

    CHECK(format::ms(12.34) == "12.3 ms");
    CHECK(format::ms(250.0) == "250 ms");
    CHECK(format::ms(1500.0) == "1.50 s");

    CHECK(format::percent(49.6) == "50%");
    CHECK(format::percent(0.0) == "0%");

    CHECK(format::note(440.0) == "A4");
    CHECK(format::note(261.6256) == "C4");
    CHECK(format::note(445.0) == "A4 +20");
    CHECK(format::note(435.0) == "A4 " + kMinus + "20");
    CHECK(format::note(27.5) == "A0");
    CHECK(format::note(8.1758) == "C-1");
    CHECK(format::note(5.0).isEmpty());
}

TEST_CASE("numbers parse with signs, units and either minus", "[format][parse]") {
    CHECK(format::parseNumber("-3") == -3.0);
    CHECK(format::parseNumber("+3.5") == 3.5);
    CHECK(format::parseNumber("3.5 dB") == 3.5);
    CHECK(format::parseNumber("3.5dB") == 3.5);
    CHECK(format::parseNumber("  -12 DB ") == -12.0);
    CHECK(format::parseNumber(kMinus + "6") == -6.0);
    CHECK(format::parseNumber(kMinus + "6.5 dB") == -6.5);
    CHECK(format::parseNumber("50%") == 50.0);
    CHECK(format::parseNumber(".5") == 0.5);
    CHECK(format::parseNumber("-.5") == -0.5);
    CHECK(format::parseNumber("0") == 0.0);
    // units products parse themselves (e.g. ms and s) are left to them, after the number
    CHECK(format::parseNumber("20 ms") == 20.0);
}

TEST_CASE("numbers parse a decimal comma", "[format][parse]") {
    CHECK(format::parseNumber("1,5") == 1.5);
    CHECK(format::parseNumber(kMinus + "2,25 dB") == -2.25);
}

TEST_CASE("text without a leading number does not parse", "[format][parse]") {
    for (const auto* junk : {"", "   ", "dB", "%", "abc", "-", "+", ".", "inf", "nan"}) {
        INFO("text: '" << junk << "'");
        CHECK_FALSE(format::parseNumber(junk).has_value());
    }
}

TEST_CASE("a number after junk does not parse", "[format][parse]") {
    // rather than reading as 0
    for (const auto* junk : {"x5", "--3", "+-3", "gain 3"}) {
        INFO("text: '" << junk << "'");
        CHECK_FALSE(format::parseNumber(junk).has_value());
    }
}

TEST_CASE("infinities do not parse as numbers", "[format][parse]") {
    // parameters have finite ranges: "-inf" is a product's word for its minimum, not a number
    CHECK_FALSE(format::parseNumber("-inf").has_value());
    CHECK_FALSE(format::parseNumber(kMinus + "inf dB").has_value());
    CHECK_FALSE(format::parseNumber("1e999").has_value());
    CHECK_FALSE(format::parseNumber("-1e999").has_value());
}

TEST_CASE("frequencies parse with and without units", "[format][parse]") {
    CHECK(format::parseFreq("1200") == 1200.0);
    CHECK(format::parseFreq("1.2k") == 1200.0);
    CHECK(format::parseFreq("1.2 kHz") == 1200.0);
    CHECK(format::parseFreq("1.2KHZ") == 1200.0);
    CHECK(format::parseFreq("1k2") == 1200.0);
    CHECK(format::parseFreq("500 Hz") == 500.0);
    CHECK(format::parseFreq("500hz") == 500.0);
    CHECK(format::parseFreq("1,5k") == 1500.0);
    CHECK(format::parseFreq(" 80 ") == 80.0);
}

TEST_CASE("frequencies parse from note names", "[format][parse]") {
    CHECK_THAT(*format::parseFreq("A4"), WithinAbs(440.0, 1e-9));
    CHECK_THAT(*format::parseFreq("a4"), WithinAbs(440.0, 1e-9));
    CHECK_THAT(*format::parseFreq("C4"), WithinRel(261.6256, 1e-5));
    CHECK_THAT(*format::parseFreq("C#3"), WithinRel(138.5913, 1e-5));
    CHECK_THAT(*format::parseFreq("Eb2"), WithinRel(77.7817, 1e-5));
    CHECK_THAT(*format::parseFreq("Bb1"), WithinRel(58.2705, 1e-5));
    CHECK_THAT(*format::parseFreq("B2"), WithinRel(123.4708, 1e-5));
    CHECK_THAT(*format::parseFreq("C-1"), WithinRel(8.1758, 1e-4));
}

TEST_CASE("frequencies that are not positive numbers or notes do not parse", "[format][parse]") {
    for (const auto* junk : {"", "hz", "khz", "k", "-100", "0", "0 Hz", "abc", "12x", "x12", "h4", "a", "c#", "bb",
                             "1e3", "A4 Hz"}) {
        INFO("text: '" << junk << "'");
        CHECK_FALSE(format::parseFreq(junk).has_value());
    }
}

TEST_CASE("a frequency with two decimal points does not parse", "[format][parse]") {
    for (const auto* junk : {"1.2.3", "1..2", "1.2.3k", "1.5k2"}) {
        INFO("text: '" << junk << "'");
        CHECK_FALSE(format::parseFreq(junk).has_value());
    }
}

TEST_CASE("formatted values parse back to what they show", "[format][parse]") {
    for (const auto hz : {20.0, 56.8, 440.0, 1230.0, 12300.0}) {
        INFO(hz << " Hz");
        CHECK_THAT(*format::parseFreq(format::freq(hz)), WithinRel(hz, 1e-9));
    }
    for (const auto db : {-30.5, -6.25, 0.0, 3.5, 18.0}) {
        INFO(db << " dB");
        CHECK_THAT(*format::parseNumber(format::gain(db)), WithinAbs(db, 1e-9));
    }
    CHECK(format::parseNumber(format::percent(42.0)) == 42.0);
}

TEST_CASE("formatting and parsing ignore the process locale", "[format][parse]") {
    const CommaLocale locale;
    // the test itself: the locale does write a decimal comma
    std::ostringstream check;
    check << std::fixed << std::setprecision(1) << 1.5;
    REQUIRE(check.str() == "1,5");
    CHECK(format::freq(1234.0) == "1.23 kHz");
    CHECK(format::gain(-3.5) == kMinus + "3.50 dB");
    CHECK(format::ms(1500.0) == "1.50 s");
    CHECK(format::parseNumber("1.5") == 1.5);
    CHECK(format::parseNumber("1,5") == 1.5);
    CHECK(format::parseFreq("1.2k") == 1200.0);
}
