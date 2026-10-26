// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <vector>

#include "engine.hpp"
#include "resampler.hpp"

namespace altvane::dialogue::dsp {
    /**
     * runs the 48 kHz engine at any host sample rate
     *
     * the input is resampled to 48 kHz, processed and resampled back. what the round trip loses (everything above
     * ~21 kHz, and the resamplers' own error) is added back from the delayed input, scaled like the ambience:
     *   out = up(engine(down(x))) + g * (x_delayed - up(down(x)_delayed))
     * so with every level at unity the output is the delayed input, exactly, at any rate
     */
    class RateBridge {
    public:
        /**
         * allocates; the engine's worker is restarted
         * @return false if no model could be loaded, or the rate is unsupported (the output is then the input)
         */
        bool prepare(double sample_rate, int num_channels, int max_block);

        void release();

        int getLatency() const { return latency_; }

        Engine& getEngine() { return engine_; }

        bool isResampling() const { return resampling_; }

        /**
         * process in place, at most the prepared block size
         * @param residual_gain how much of what the 48 kHz round trip loses goes to the output (the ambience level)
         */
        void process(juce::AudioBuffer<float>& buffer, bool realtime, float residual_gain);

        /**
         * the input delayed by the latency, for the bypass cross-fade; valid after process() for its samples
         */
        const float* getDelayedDry(int channel) const { return dry_out_[static_cast<size_t>(channel)].data(); }

    private:
        struct Fifo {
            std::vector<float> data;
            int read{0}, size{0};

            void push(const float* samples, int count);
            void pop(float* dest, int count);
            void prefill(int count);
        };

        Engine engine_;
        int num_channels_{0}, max_block_{0}, latency_{0}, max_block_48_{0};
        bool resampling_{false}, supported_{true};

        std::array<Resampler, 2> down_, up_, up_dry_;
        std::array<std::vector<float>, 2> stage_, stage_dry_, up_out_;
        std::array<Fifo, 2> out_fifo_, round_trip_fifo_;
        // delay lines: the host-rate input (for the residual and bypass) and the 48 kHz dry signal (for the round trip)
        std::array<std::vector<float>, 2> host_delay_, dry48_delay_;
        int host_delay_pos_{0}, dry48_delay_pos_{0};
        std::array<std::vector<float>, 2> dry_out_, round_trip_;
        std::vector<float*> engine_ptrs_;
    };
}
