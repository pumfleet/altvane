// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "value_format.hpp"

#include <array>
#include <cmath>

namespace contour::ui::format {
    namespace {
        constexpr std::array kNoteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

        juce::String normalise(const juce::String& text) {
            return text.trim().replace(kMinus, "-").replace(",", ".").toLowerCase();
        }

        juce::String signedNumber(const double v, const int decimals, const bool force_sign) {
            const auto rounded = std::round(v * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
            const auto magnitude = juce::String(std::abs(rounded), decimals);
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
        if (hz < 100.0) {
            s = juce::String(hz, 1);
            unit = " Hz";
        } else if (hz < 1000.0) {
            s = juce::String(juce::roundToInt(hz));
            unit = " Hz";
        } else if (hz < 10000.0) {
            s = juce::String(hz / 1000.0, 2);
            unit = " kHz";
        } else {
            s = juce::String(hz / 1000.0, 1);
            unit = " kHz";
        }
        return with_unit ? s + unit : s;
    }

    juce::String gain(const double db, const bool with_unit, const bool force_sign) {
        const auto s = signedNumber(db, std::abs(db) < 10.0 ? 2 : 1, force_sign);
        return with_unit ? s + " dB" : s;
    }

    juce::String q(const double q) {
        return juce::String(q, q < 10.0 ? 2 : 1);
    }

    juce::String ms(const double ms) {
        if (ms >= 1000.0) {
            return juce::String(ms / 1000.0, 2) + " s";
        }
        return juce::String(ms, ms < 100.0 ? 1 : 0) + " ms";
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
        if (t.isEmpty() || !t.containsAnyOf("0123456789")) {
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
        if (!t.containsAnyOf("0123456789") || !t.containsOnly("0123456789.")) {
            return std::nullopt;
        }
        const auto v = t.getDoubleValue() * scale;
        return v > 0.0 ? std::optional<double>{v} : std::nullopt;
    }
}
