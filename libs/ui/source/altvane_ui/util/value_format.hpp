// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <optional>
#include <juce_core/juce_core.h>

namespace altvane::ui::format {
    // typographic minus sign, reads better than a hyphen next to tabular digits
    inline const juce::String kMinus = juce::String::charToString(static_cast<juce::juce_wchar>(0x2212));

    juce::String freq(double hz, bool with_unit = true);

    juce::String gain(double db, bool with_unit = true, bool force_sign = true);

    juce::String q(double q);

    juce::String ms(double ms);

    juce::String percent(double p);

    juce::String note(double hz);

    /**
     * parse user input such as "1.2k", "1200", "1.2 kHz", "A4", "C#3"
     */
    std::optional<double> parseFreq(const juce::String& text);

    /**
     * parse "-3", "+3.5", "3.5 dB", using either a hyphen or a minus sign
     */
    std::optional<double> parseNumber(const juce::String& text);
}
