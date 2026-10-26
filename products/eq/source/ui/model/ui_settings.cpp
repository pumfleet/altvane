// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include "ui_settings.hpp"

#include "altvane_ui/util/spring.hpp"
#include "../theme/eq_tokens.hpp"

namespace altvane::eq {
    void UISettings::load(const juce::PropertiesFile& file) {
        scale = std::clamp(static_cast<float>(file.getDoubleValue("interface_scale", 1.0)), kScales.front(), kScales.back());
        accent = std::clamp(file.getIntValue("accent", 0), 0, static_cast<int>(tokens::colour::kAccentChoices.size()) - 1);
        reduce_motion = file.getBoolValue("reduce_motion", false);
        spectrum_grab = file.getBoolValue("spectrum_grab", true);
        drag_sensitivity = std::clamp(static_cast<float>(file.getDoubleValue("drag_sensitivity", 1.0)), .25f, 4.f);
        wheel_sensitivity = std::clamp(static_cast<float>(file.getDoubleValue("wheel_sensitivity", 1.0)), .25f, 4.f);
    }

    void UISettings::save(juce::PropertiesFile& file) const {
        file.setValue("interface_scale", static_cast<double>(scale));
        file.setValue("accent", accent);
        file.setValue("reduce_motion", reduce_motion);
        file.setValue("spectrum_grab", spectrum_grab);
        file.setValue("drag_sensitivity", static_cast<double>(drag_sensitivity));
        file.setValue("wheel_sensitivity", static_cast<double>(wheel_sensitivity));
    }

    void UISettings::apply() const {
        tokens::colour::kAccent = tokens::colour::kAccentChoices[static_cast<size_t>(accent)].second;
        InteractionSettings::reduce_motion = reduce_motion;
        InteractionSettings::drag_sensitivity = drag_sensitivity;
        InteractionSettings::wheel_sensitivity = wheel_sensitivity;
    }
}
