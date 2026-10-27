// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/widgets/widgets.hpp>

#include "../PluginProcessor.hpp"

namespace altvane::leveler {
    /**
     * the rider fader: follows the gain the engine applies; grab it to ride by hand (in Read mode, to edit the
     * automation); the range handles (left) and the idle marker (right) drag too
     */
    class RiderFader final : public ui::ValueControl {
    public:
        RiderFader(PluginProcessor& p, ui::ParamEditor& editor);

        /**
         * finishes a ride still in progress (a wheel gesture ends after a pause), so closing the editor never leaves
         * the processor under manual override or the host inside a gesture
         */
        ~RiderFader() override;

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        void mouseMove(const juce::MouseEvent& e) override;

        void mouseExit(const juce::MouseEvent& e) override;

        void mouseDown(const juce::MouseEvent& e) override;

        void mouseDrag(const juce::MouseEvent& e) override;

        void mouseUp(const juce::MouseEvent& e) override;

        /**
         * the parameter under the mouse: a range handle or the idle marker, otherwise the fader itself
         */
        juce::AudioProcessorParameter* getHoveredParameter() const;

    protected:
        juce::Rectangle<int> getTextArea() const override { return readout_.toNearestInt(); }

    private:
        enum class Handle { kNone, kMin, kMax, kIdle };

        PluginProcessor& p_ref_;
        ui::ParamEditor& editor_;
        juce::Rectangle<float> track_, readout_;
        float scale_{12.f};
        Handle hover_{Handle::kNone}, drag_{Handle::kNone};
        bool read_gesture_{false}, riding_{false};
        std::array<float, 4> look_{};

        float dbToY(float db) const;
        float yToDB(float y) const;
        float param(const char* id) const;
        Handle handleAt(juce::Point<float> position) const;
        juce::RangedAudioParameter* parameterFor(Handle handle) const;
    };

    /**
     * the right-hand column: automation mode, ride state, the fader and its range in numbers
     */
    class RiderPanel final : public juce::Component {
    public:
        RiderPanel(PluginProcessor& p, ui::ParamEditor& editor);

        void tick();

        void paint(juce::Graphics& g) override;

        void resized() override;

        RiderFader& getFader() { return fader_; }

        ui::Chip& getModeChip(dsp::Mode mode) { return *chips_[static_cast<size_t>(mode)]; }

    private:
        PluginProcessor& p_ref_;
        RiderFader fader_;
        ui::Chip off_{"Off"}, write_{"Write"}, read_{"Read"};
        std::array<ui::Chip*, 3> chips_{&off_, &write_, &read_};
        ui::ValueField max_, min_, idle_;
        juce::Rectangle<float> title_area_, state_area_;
        dsp::HopState state_{dsp::HopState::kSilent};
        bool writing_{false};
    };
}
