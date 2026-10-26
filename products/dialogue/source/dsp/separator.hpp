// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <memory>
#include <vector>
#include <juce_core/juce_core.h>

#include "spectral.hpp"

namespace altvane::dialogue::dsp {
    enum class Quality { kEco, kHigh };

    /**
     * a streaming speech-estimation model, loaded once per process and shared by every instance
     * (DPDFNet: one STFT frame in, the speech estimate of the frame kModelLag hops earlier out,
     * with its recurrent state passed in and out explicitly)
     */
    class SeparatorModel {
    public:
        /**
         * the shared model for a quality; loads it on first use (allocates, so not on the audio thread)
         * @return nullptr if the model could not be loaded
         */
        static std::shared_ptr<SeparatorModel> get(Quality quality);

        virtual ~SeparatorModel() = default;

        size_t getStateSize() const { return initial_state_.size(); }

        const std::vector<float>& getInitialState() const { return initial_state_; }

        juce::String getName() const { return name_; }

        /**
         * run one frame; thread-safe, but allocates inside ONNX Runtime, so only from worker threads
         * @param spec_in / spec_out kBins interleaved (re, im) pairs
         * @return false if inference failed
         */
        bool run(const float* spec_in, const float* state_in, float* spec_out, float* state_out) const;

    protected:
        SeparatorModel() = default;

        juce::String name_;
        std::vector<float> initial_state_;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        friend struct ModelLoader;
    };

    /**
     * one stream through a shared model: its recurrent state, preallocated
     */
    class SeparatorStream {
    public:
        explicit SeparatorStream(std::shared_ptr<SeparatorModel> model);

        void reset();

        /**
         * @param in the frame just analysed
         * @param speech receives the speech estimate of the frame kModelLag hops earlier
         * @return false if the model failed; speech is then left unchanged
         */
        bool process(std::span<const Complex, kBins> in, std::span<Complex, kBins> speech);

        const SeparatorModel& getModel() const { return *model_; }

    private:
        std::shared_ptr<SeparatorModel> model_;
        std::vector<float> state_a_, state_b_;
        bool a_is_input_{true};
        std::array<float, kBins * 2> spec_out_{};
    };
}
