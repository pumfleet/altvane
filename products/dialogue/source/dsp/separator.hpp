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

#include "dpdfnet.hpp"
#include "spectral.hpp"

namespace altvane::dialogue::dsp {
    enum class Quality { kEco, kHigh };

    /**
     * activations for running the separation, one per thread that runs it (allocates, so not on the audio thread)
     */
    using SeparatorScratch = DpdfNet::Scratch;

    /**
     * a streaming speech-estimation model, loaded once per process and shared by every instance
     * (DPDFNet: one STFT frame in, the speech estimate of the frame kModelLag hops earlier out)
     */
    class SeparatorModel {
    public:
        static constexpr int kMaxStreams = DpdfNet::kMaxStreams;

        /**
         * the shared model for a quality; loads it on first use (allocates, so not on the audio thread)
         * @return nullptr if the model could not be loaded
         */
        static std::shared_ptr<SeparatorModel> get(Quality quality);

        juce::String getName() const { return name_; }

        const DpdfNet& getNetwork() const { return network_; }

    private:
        struct Token {
        };

    public:
        SeparatorModel(Token, std::span<const std::byte> onnx, juce::String name);

    private:
        DpdfNet network_;
        juce::String name_;
    };

    /**
     * one or more streams through a shared model (the channels of Stereo mode, or the mid of Linked), with their
     * recurrent states preallocated; the streams of a frame run together, so each weight is read once per frame
     */
    class SeparatorStreams {
    public:
        SeparatorStreams(std::shared_ptr<SeparatorModel> model, int count);

        void reset();

        int size() const { return static_cast<int>(states_.size()); }

        /**
         * thread-safe for distinct streams and scratch; never allocates
         * @param in each stream's frame just analysed
         * @param speech receives each stream's speech estimate of the frame kModelLag hops earlier
         */
        void process(std::span<const Spectrum* const> in, std::span<Spectrum* const> speech, SeparatorScratch& scratch);

        const SeparatorModel& getModel() const { return *model_; }

    private:
        std::shared_ptr<SeparatorModel> model_;
        std::vector<DpdfNet::State> states_;
    };
}
