// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <altvane_ui/session/param_editor.hpp>
#include <altvane_ui/session/session.hpp>

namespace altvane::ui::test {
    namespace ids {
        inline constexpr auto kGain = "gain";
        inline constexpr auto kFreq = "freq";
        inline constexpr auto kMode = "mode";
        inline constexpr auto kBypass = "bypass";
        // a fader the engine moves: products leave such parameters out of presets
        inline constexpr auto kMeter = "meter";
    }

    /**
     * a processor with a handful of parameters of each kind, its state being the parameter tree,
     * plus the editor-side objects a product's processor keeps (Session) and its editor makes (ParamEditor)
     */
    class FakeProcessor final : public juce::AudioProcessor {
    public:
        juce::AudioProcessorValueTreeState parameters_;
        Session session_;
        ParamEditor editor_{session_.undo_manager};

        FakeProcessor() :
            AudioProcessor(BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
            parameters_(*this, nullptr, "FakeState", createLayout()) {
        }

        juce::RangedAudioParameter* para(const char* id) const { return parameters_.getParameter(id); }

        float plain(const char* id) const { return parameters_.getRawParameterValue(id)->load(); }

        /**
         * set a parameter by its plain value, as automation would (outside the editor and its history)
         */
        void setPlain(const char* id, const float v) const {
            auto* p = para(id);
            p->setValueNotifyingHost(p->convertTo0to1(v));
        }

        const juce::String getName() const override { return "Fake"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}

        void getStateInformation(juce::MemoryBlock& dest) override {
            if (const auto xml = parameters_.copyState().createXml()) {
                copyXmlToBinary(*xml, dest);
            }
        }

        void setStateInformation(const void* data, const int size) override {
            if (const auto xml = getXmlFromBinary(data, size); xml != nullptr
                && xml->hasTagName(parameters_.state.getType())) {
                parameters_.replaceState(juce::ValueTree::fromXml(*xml));
            }
        }

    private:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{ids::kGain, 1}, "Gain", juce::NormalisableRange<float>(-24.f, 24.f, .01f), 0.f));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{ids::kFreq, 1}, "Freq",
                juce::NormalisableRange<float>(20.f, 20000.f, 0.f, .25f), 1000.f));
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{ids::kMode, 1}, "Mode", juce::StringArray{"Clean", "Warm", "Hot"}, 0));
            layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::kBypass, 1}, "Bypass", false));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{ids::kMeter, 1}, "Meter", juce::NormalisableRange<float>(0.f, 1.f), .5f));
            return layout;
        }
    };

    /**
     * counts a parameter's host gestures: every begin must be matched by an end
     */
    class GestureCounter final : private juce::AudioProcessorParameter::Listener {
    public:
        int begins{0}, ends{0}, changes{0};

        explicit GestureCounter(juce::AudioProcessorParameter* p) : p_(p) { p_->addListener(this); }

        ~GestureCounter() override { p_->removeListener(this); }

    private:
        juce::AudioProcessorParameter* p_;

        void parameterValueChanged(int, float) override { ++changes; }

        void parameterGestureChanged(int, const bool starting) override { ++(starting ? begins : ends); }
    };

    /**
     * a fresh, empty folder (by default under the system's temporary folder), deleted when the test ends
     */
    class TempDir final {
    public:
        explicit TempDir(const juce::File& parent = juce::File::getSpecialLocation(juce::File::tempDirectory)) :
            // unique, not just unused: ctest -j runs each test case in its own process at the same time
            dir_(parent.getChildFile("altvane_ui_tests_" + juce::Uuid().toString())) {
            dir_.createDirectory();
        }

        ~TempDir() { dir_.deleteRecursively(); }

        const juce::File& get() const { return dir_; }

    private:
        juce::File dir_;
    };
}
