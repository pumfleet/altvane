// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "value_format.hpp"

#include <array>
#include <cmath>

namespace altvane::ui::format {
    namespace {
        constexpr std::array kNoteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

        juce::String normalise(const juce::String& text) {
            return text.trim().replace(kMinus, "-").replace(",", ".").toLowerCase();
        }

        /**
         * whether v, shown with this many decimals, reads below limit: picks a format by the number as shown,
         * so a value that rounds up to a boundary (99.97 Hz) reads like the boundary ("100 Hz", not "100.0 Hz")
         */
        bool showsBelow(const double v, const double limit, const int decimals) {
            const auto scale = std::pow(10.0, decimals);
            return std::round(v * scale) / scale < limit;
        }

        /**
         * v with this many decimals and always a decimal point: juce::String(v, decimals) follows the global C++
         * locale, which a host may set ("1,23" in German), and prints every decimal when decimals is 0
         */
        juce::String fixed(const double v, const int decimals) {
            const auto scale = std::pow(10.0, decimals);
            if (!std::isfinite(v) || std::abs(v) * scale >= 1e15) {
                return std::isnan(v) ? juce::String("nan") : std::isinf(v) ? juce::String(v < 0.0 ? "-inf" : "inf")
                                                                           : juce::String(v, decimals);
            }
            const auto n = std::llround(std::abs(v) * scale);
            const auto unit = std::llround(scale);
            auto s = juce::String(n / unit);
            if (decimals > 0) {
                s << "." << juce::String(n % unit).paddedLeft('0', decimals);
            }
            return v < 0.0 && n != 0 ? "-" + s : s;
        }

        juce::String signedNumber(const double v, const int decimals, const bool force_sign) {
            const auto rounded = std::round(v * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
            const auto magnitude = fixed(std::abs(rounded), decimals);
            if (rounded < 0.0) {
                return kMinus + magnitude;
            }
            if (force_sign && rounded > 0.0) {
                return "+" + magnitude;
            }
            return magnitude;
        }
    }

    juce::String freq(const double hz, const bool with_unit) {
        juce::String s;
        juce::String unit;
        if (showsBelow(hz, 100.0, 1)) {
            s = fixed(hz, 1);
            unit = " Hz";
        } else if (showsBelow(hz, 1000.0, 0)) {
            s = juce::String(juce::roundToInt(hz));
            unit = " Hz";
        } else if (showsBelow(hz / 1000.0, 10.0, 2)) {
            s = fixed(hz / 1000.0, 2);
            unit = " kHz";
        } else {
            s = fixed(hz / 1000.0, 1);
            unit = " kHz";
        }
        return with_unit ? s + unit : s;
    }

    juce::String gain(const double db, const bool with_unit, const bool force_sign) {
        const auto s = signedNumber(db, showsBelow(std::abs(db), 10.0, 2) ? 2 : 1, force_sign);
        return with_unit ? s + " dB" : s;
    }

    juce::String q(const double q) {
        return fixed(q, showsBelow(q, 10.0, 2) ? 2 : 1);
    }

    juce::String ms(const double ms) {
        if (!showsBelow(ms, 1000.0, 0)) {
            return fixed(ms / 1000.0, 2) + " s";
        }
        return fixed(ms, showsBelow(ms, 100.0, 1) ? 1 : 0) + " ms";
    }

    juce::String percent(const double p) {
        return juce::String(juce::roundToInt(p)) + "%";
    }

    juce::String note(const double hz) {
        if (hz < 8.0) {
            return {};
        }
        const auto semis_from_a4 = 12.0 * std::log2(hz / 440.0);
        const auto rounded = static_cast<int>(std::round(semis_from_a4));
        const auto cents = static_cast<int>(std::round((semis_from_a4 - rounded) * 100.0));
        const auto from_c0 = rounded + 57;
        const auto octave = static_cast<int>(std::floor(static_cast<double>(from_c0) / 12.0));
        const auto idx = ((from_c0 % 12) + 12) % 12;
        auto s = juce::String(kNoteNames[static_cast<size_t>(idx)]) + juce::String(octave);
        if (cents != 0) {
            s << " " << (cents > 0 ? "+" : kMinus) << juce::String(std::abs(cents));
        }
        return s;
    }

    std::optional<double> parseNumber(const juce::String& text) {
        const auto t = normalise(text).removeCharacters(" db%");
        // the number comes first ("-3", "+.5"); text after it, such as a unit, is the caller's
        const auto unsigned_part = t.startsWithChar('-') || t.startsWithChar('+') ? t.substring(1) : t;
        const auto digits = unsigned_part.startsWithChar('.') ? unsigned_part.substring(1) : unsigned_part;
        if (!juce::CharacterFunctions::isDigit(digits[0])) {
            return std::nullopt;
        }
        const auto v = t.getDoubleValue();
        return std::isfinite(v) ? std::optional<double>{v} : std::nullopt;
    }

    std::optional<double> parseFreq(const juce::String& text) {
        auto t = normalise(text).removeCharacters(" ");
        if (t.isEmpty()) {
            return std::nullopt;
        }
        // note names, e.g. a4, c#3, eb2
        if (juce::CharacterFunctions::isLetter(t[0]) && t[0] >= 'a' && t[0] <= 'g') {
            static constexpr std::array<int, 7> kSemis{9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
            auto semi = kSemis[static_cast<size_t>(t[0] - 'a')];
            int pos = 1;
            if (t[pos] == '#') {
                ++semi;
                ++pos;
            } else if (t[pos] == 'b' && t.length() > pos + 1) {
                --semi;
                ++pos;
            }
            const auto octave_text = t.substring(pos);
            if (octave_text.isEmpty() || !octave_text.containsOnly("-0123456789")) {
                return std::nullopt;
            }
            const auto from_c0 = octave_text.getIntValue() * 12 + semi;
            return 440.0 * std::pow(2.0, static_cast<double>(from_c0 - 57) / 12.0);
        }
        double scale = 1.0;
        if (t.endsWith("khz")) {
            t = t.dropLastCharacters(3);
            scale = 1000.0;
        } else if (t.endsWith("hz")) {
            t = t.dropLastCharacters(2);
        } else if (t.endsWith("k")) {
            t = t.dropLastCharacters(1);
            scale = 1000.0;
        } else if (t.containsChar('k')) {
            // "1k2" style
            t = t.replaceCharacter('k', '.');
            scale = 1000.0;
        }
        if (!t.containsAnyOf("0123456789") || !t.containsOnly("0123456789.") || t.indexOfChar('.') != t.lastIndexOfChar('.')) {
            return std::nullopt;
        }
        const auto v = t.getDoubleValue() * scale;
        return v > 0.0 ? std::optional<double>{v} : std::nullopt;
    }
}
