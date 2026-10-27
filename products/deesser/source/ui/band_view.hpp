// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/util/spring.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "../PluginProcessor.hpp"
#include "analyzer.hpp"

namespace altvane::deesser {
    /**
     * where the de-esser listens and what it does there: the spectrum (the detection signal, the output and the
     * esses heard so far), the detection band (drag its edges, or drag it whole; the wheel widens it), the gain
     * the processing applies right now (a shelf, a bell or the whole signal) and the sibilance finder's pin,
     * which fits the band to the esses with a click
     */
    class BandView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
    public:
        enum class Target { kNone, kLow, kHigh, kBand, kFinder };

        BandView(PluginProcessor& p, ui::ParamEditor& editor, Analyzer& analyzer);

        void tick(double dt, float reduction_db, bool new_spectrum);

        /**
         * fit the band to the sibilance the finder has heard (one undo step)
         * @return false if the finder has nothing yet
         */
        bool fitToFinder();

        /**
         * the parameter under the mouse (a band edge), or -1
         */
        int getHoveredParameterIndex() const;

        float freqX(float hz) const;

        float xFreq(float x) const;

        juce::Rectangle<float> getPlot() const { return plot_; }

        ui::ValueField& getLowField() { return low_field_; }

        ui::ValueField& getHighField() { return high_field_; }

        void hoverForTesting(juce::Point<float> p);

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    private:
        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        Analyzer& analyzer_;

        // the edges as values: typed, dragged, read by screen readers
        ui::ValueField low_field_, high_field_;
        juce::Rectangle<float> plot_, title_area_, caption_area_, finder_pill_;
        juce::Image grid_;
        float low_{5500.f}, high_{20000.f}, range_{9.f};
        bool open_{true}, wide_{false}, bell_{false};
        ui::Spring reduction_{0.f, 14.f};
        ui::Fade finder_fade_{.24f};
        Analyzer::Finder finder_;

        Target hovered_{Target::kNone}, dragged_{Target::kNone};
        float drag_low_{0.f}, drag_high_{0.f}, drag_start_hz_{0.f};
        bool wheel_active_{false};
        // the edit this view opened (a drag or the wheel), so it only ever ends its own
        uint64_t edit_id_{0}, field_edit_id_{0};

        float spectrumY(float db, float hz) const;
        float responseY(float db) const;
        float responseDb(float hz, float reduction_db) const;
        juce::RangedAudioParameter* param(const char* id) const;
        Target hit(juce::Point<float> p) const;
        void setBand(float low, float high, bool low_wins);
        void rebuildGrid();
        void updateTooltip();
        juce::Path spectrumPath(const Analyzer::Curve& curve, bool closed) const;
        void paintBand(juce::Graphics& g);
        void paintResponse(juce::Graphics& g);
        void paintFinder(juce::Graphics& g);
        void timerCallback() override;
    };
}
