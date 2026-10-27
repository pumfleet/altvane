// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "altvane_ui/util/value_format.hpp"

/**
 * how the compressor writes its values
 */
namespace altvane::compressor::formats {
    inline juce::String number(const float v, const int decimals) {
        const auto s = juce::String(std::abs(v), decimals);
        return v < 0.f && s.getDoubleValue() != 0.0 ? ui::format::kMinus + s : s;
    }

    // "−32.0 dB"
    inline juce::String db(const float v) {
        return number(v, std::abs(v) < 100.f ? 1 : 0) + " dB";
    }

    // "+3.0 dB", for gains around zero
    inline juce::String signedDB(const float v) {
        return (v >= .05f ? "+" : "") + db(v);
    }

    // "3.00:1", "100:1"
    inline juce::String ratio(const float v) {
        if (v >= 99.95f) {
            return "100:1";
        }
        return juce::String(v, v < 10.f ? 2 : 1) + ":1";
    }

    // "0.20 ms", "12.5 ms", "500 ms", "1.20 s"
    inline juce::String ms(const float v) {
        const auto a = std::abs(v);
        if (a >= 1000.f) {
            return number(v / 1000.f, 2) + " s";
        }
        return number(v, a < 10.f ? 2 : a < 100.f ? 1 : 0) + " ms";
    }

    inline juce::String percent(const float v) {
        return juce::String(juce::roundToInt(v)) + "%";
    }

    /**
     * a time typed as "20", "20 ms" or "1.2 s"
     */
    inline std::optional<float> parseMs(const juce::String& text) {
        const auto v = ui::format::parseNumber(text);
        if (!v.has_value()) {
            return std::nullopt;
        }
        const auto t = text.trim().toLowerCase();
        const auto seconds = t.endsWith("s") && !t.endsWith("ms");
        return static_cast<float>(seconds ? *v * 1000.0 : *v);
    }
}
