// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <numbers>

#include "altvane_ui/widgets/widgets.hpp"
#include "../../dsp/filter/ideal_filter/ideal.hpp"
#include "../analysis/side_spectrum.hpp"
#include "../model/comp_bridge.hpp"

namespace altvane::compressor {
    /**
     * the side-chain EQ: eight bands over the spectrum the detector hears
     * double-click to add a band, drag to move, wheel for Q (or slope), Alt-click to bypass,
     * right-click for the shape, slope, solo and delete
     * the selected band's shape and values are also shown along the top, where they can be typed, and the keyboard
     * reaches everything: Return adds a band, 1 to 8 select one, arrows move it, hold S to solo it
     */
    class SideEqView final : public juce::Component, private juce::Timer {
    public:
        SideEqView(CompBridge& bridge, SideSpectrum& spectrum);

        ~SideEqView() override;

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        bool keyPressed(const juce::KeyPress& key) override;
        bool keyStateChanged(bool is_key_down) override;
        void focusGainedWithDirection(FocusChangeType cause, FocusChangeDirection direction) override;
        void focusLost(FocusChangeType cause) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        /**
         * whether any band is in use
         */
        bool hasBands() const;

        /**
         * the band the controls along the top edit, or -1
         */
        int getSelected() const { return selected_; }

        void select(int band);

        /**
         * add a band in the next free slot (a peak, or a cut near either end), select it and return it, or -1
         */
        int addBand(float freq, float gain);

        /**
         * band solo: the output plays the side chain through the band's region (the engine's solo filter);
         * zlp::kBandNum turns it off
         */
        void setSolo(size_t band);

        size_t getSolo() const { return solo_band_; }

        /**
         * the selected band in words, for assistive technology
         */
        juce::String describeSelection() const;

        /**
         * room left free at the top left, for the drawer's EQ switch
         */
        void setToolsInset(int x);

        ui::ValueField& getFreqFieldForTesting() { return freq_; }
        ui::Chip& getSoloChipForTesting() { return solo_; }

    private:
        struct Band {
            int status{0}, type{0}, order{1};
            float freq{1000.f}, gain{0.f}, q{.707f};

            bool operator==(const Band& o) const {
                return status == o.status && type == o.type && order == o.order && juce::exactlyEqual(freq, o.freq)
                       && juce::exactlyEqual(gain, o.gain) && juce::exactlyEqual(q, o.q);
            }

            bool isUsed() const { return status != 0; }

            bool isOn() const { return status == 2; }

            bool hasGain() const;

            bool hasSlope() const;
        };

        // what the wheel or the arrow keys are editing, ended by a timer so that a burst is one undo step
        enum class TimedEdit { kNone, kQ, kMove };

        static constexpr float kPadX = 14.f, kPadTop = 30.f, kPadBottom = 18.f;
        static constexpr float kMinFreq = 20.f, kMaxFreq = 20000.f;

        CompBridge& bridge_;
        SideSpectrum& spectrum_;
        std::array<Band, zlp::kBandNum> bands_{};
        std::array<std::vector<float>, zlp::kBandNum> band_db_;
        std::vector<float> sum_db_, ws_;
        zldsp::filter::Ideal<float, 16> ideal_;
        double sample_rate_{0.0};
        float max_db_{12.f};
        bool eq_on_{true}, needs_response_{true}, focus_ring_{false}, key_solo_{false};
        int hover_{-1}, drag_{-1}, selected_{-1}, tools_inset_{64};
        TimedEdit timed_edit_{TimedEdit::kNone};
        int timed_band_{-1};
        uint64_t timed_edit_id_{0};
        size_t solo_band_{zlp::kBandNum};
        juce::Point<float> drag_start_;
        float drag_start_freq_{1000.f}, drag_start_gain_{0.f};
        juce::Path spectrum_path_;

        // the selected band's controls, along the top
        ui::Selector shape_, slope_;
        ui::ValueField freq_, gain_, q_;
        ui::Chip solo_{"Solo"};

        juce::Rectangle<float> getPlot() const;
        float xOf(float freq) const;
        float freqOf(float x) const;
        float yOf(float db) const;
        float dbOf(float y) const;
        juce::Point<float> nodePosition(size_t band) const;
        int bandAt(juce::Point<float> p) const;
        std::pair<float, float> getSoloRange(size_t band) const;
        void updateResponse();
        void buildSpectrumPath();
        juce::RangedAudioParameter* para(const char* id, size_t band) const;
        void setBand(size_t band, std::initializer_list<std::pair<const char*, float>> values, const juce::String& name);
        juce::PopupMenu buildMenu(size_t band) const;
        void applyMenu(size_t band, int result);
        void showMenu(size_t band);
        void bindStrip();
        void updateStrip();
        void layoutStrip();
        void nudge(float octaves, float db);
        void beginTimedEdit(TimedEdit kind, int band);
        void endTimedEdit();
        void notifySelection();
        void timerCallback() override;
    };
}
