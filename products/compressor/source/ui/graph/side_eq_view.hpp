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

#include "../../dsp/filter/ideal_filter/ideal.hpp"
#include "../analysis/side_spectrum.hpp"
#include "../model/comp_bridge.hpp"

namespace altvane::compressor {
    /**
     * the side-chain EQ: eight bands over the spectrum the detector hears
     * double-click to add a band, drag to move, wheel for Q (or slope), Alt-click to bypass,
     * right-click for the shape, slope and delete
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

        /**
         * whether any band is in use
         */
        bool hasBands() const;

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
        };

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
        bool eq_on_{true}, needs_response_{true};
        int hover_{-1}, drag_{-1}, wheel_band_{-1};
        juce::Point<float> drag_start_;
        float drag_start_freq_{1000.f}, drag_start_gain_{0.f};
        juce::Path spectrum_path_;

        juce::Rectangle<float> getPlot() const;
        float xOf(float freq) const;
        float freqOf(float x) const;
        float yOf(float db) const;
        float dbOf(float y) const;
        juce::Point<float> nodePosition(size_t band) const;
        int bandAt(juce::Point<float> p) const;
        void updateResponse();
        void buildSpectrumPath();
        juce::RangedAudioParameter* para(const char* id, size_t band) const;
        void setBand(size_t band, std::initializer_list<std::pair<const char*, float>> values, const juce::String& name);
        void showMenu(size_t band);
        void timerCallback() override;
    };
}
