// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "common.hpp"

namespace altvane::limiter {
    /**
     * the loudness card: a delivery target, integrated loudness against it, the other EBU R128 values,
     * the short-term distribution, and one click to move the gain onto the target
     */
    class LoudnessPanel final : public juce::Component {
    public:
        LoudnessPanel(PluginProcessor& p, ui::ParamEditor& editor);

        void tick(const LoudnessReadings& loudness, float correlation);

        /**
         * the height the panel wants; without the histogram when space is short
         */
        static float getPreferredHeight(bool custom, bool histogram);

        void paint(juce::Graphics& g) override;

        void resized() override;

        /**
         * move the gain by the difference to the target and start a new measurement (one undo step)
         */
        void matchTarget();

        std::optional<float> getGainToTarget() const;

        ui::Selector& getTargetSelectorForTesting() { return target_; }

    private:
        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        ui::Selector target_;
        ui::Chip reset_{"Reset"}, auto_{"Auto"};
        ui::ValueField custom_lufs_, custom_peak_;
        ui::PillButton match_{"Match target", true};

        LoudnessReadings r_;
        float correlation_{std::numeric_limits<float>::quiet_NaN()};
        LoudnessTarget current_{kLoudnessTargets[0]};
        bool custom_{false}, show_histogram_{true};
        juce::Rectangle<float> header_, integrated_, stats_, histogram_;
        juce::String last_text_;

        void bindCustom(ui::ValueField& field, const char* id, const juce::String& name, const juce::String& unit);
        void paintStats(juce::Graphics& g) const;
        void paintHistogram(juce::Graphics& g) const;
    };
}
