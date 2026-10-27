// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <deque>

#include "../analysis/transfer_model.hpp"

namespace altvane::compressor {
    /**
     * the transfer curve (input level → output level) in a floating glass panel
     * the curve lights up to the detector's current level, with a dot and a short trail;
     * drag the elbow for threshold, the upper slope for ratio and the shoulders for knee width
     */
    class KneeDisplay final : public juce::Component, private juce::Timer {
    public:
        KneeDisplay(CompBridge& bridge, TransferModel& model);

        ~KneeDisplay() override;

        /**
         * @param detector_db the side-chain level the detector sees, or nothing when no audio arrived
         */
        void tick(double dt, std::optional<float> detector_db);

        void paint(juce::Graphics& g) override;

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    private:
        enum class Handle { kNone, kElbow, kRatio, kKneeLow, kKneeHigh };

        static constexpr float kPad = 30.f, kEdge = 14.f;

        CompBridge& bridge_;
        TransferModel& model_;
        float scale_db_{54.f};
        float dot_db_{-120.f};
        std::deque<float> trail_;
        Handle hover_{Handle::kNone}, drag_{Handle::kNone};
        std::optional<float> hover_input_;
        float drag_offset_{0.f}, last_drag_y_{0.f};
        juce::String wheel_id_;

        juce::Rectangle<float> getPlot() const;
        float xOf(float db) const;
        float yOf(float db) const;
        float dbOfX(float x) const;
        float dbOfY(float y) const;
        juce::Point<float> handlePosition(Handle h);
        Handle handleAt(juce::Point<float> p);
        static juce::String parameterFor(Handle h);
        void timerCallback() override;
    };
}
