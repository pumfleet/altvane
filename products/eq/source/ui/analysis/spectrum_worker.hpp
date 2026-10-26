// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "../common.hpp"
#include "altvane_ui/util/tri_buffer.hpp"
#include "altvane_ui/util/freq_scale.hpp"
#include "../../PluginProcessor.hpp"
#include "../../dsp/analyzer/fft_analyzer/fft_analyzer_receiver.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_smoother.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_tilter.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_decayer.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_blender.hpp"
#include "../../dsp/analyzer/fft_analyzer/spectrum_collision.hpp"
#include "../../chore/thread/notifier.hpp"
#include "match_engine.hpp"

namespace altvane::eq {
    /**
     * drains the processor's analyzer FIFO on its own thread and turns it into spectrum paths
     * the analysis chain (multi-resolution FFT, blend, smoothing, tilt, decay) is the upstream one,
     * adapted from zlpanel::FFTPanel; only the geometry is Contour's
     */
    class SpectrumWorker final : private juce::Thread {
    public:
        enum Source { kPre, kPost, kSide, kNumSources };

        struct Paths {
            juce::Path fill; // closed below the graph
            juce::Path line; // the open top edge, for stroking
        };

        struct Peak {
            float x, y;
            float freq, db;
        };

        static constexpr size_t kMaxPeaks = 24;

        explicit SpectrumWorker(PluginProcessor& p, MatchEngine* match = nullptr);

        ~SpectrumWorker() override;

        void start();

        void stop();

        /**
         * wake the worker for one analysis pass, called every frame
         */
        void trigger() { notify(); }

        void setBounds(juce::Rectangle<float> graph_bounds);

        void setRefreshRate(double hz);

        /**
         * fetch the latest paths, returns true if any changed
         */
        bool pull();

        const Paths& getPaths(const Source s) const { return paths_[static_cast<size_t>(s)].getReader(); }

        bool isSourceOn(const Source s) const { return source_on_[static_cast<size_t>(s)]; }

        /**
         * peak level of the output since the last call, in dB
         */
        float takeOutputPeak(size_t channel);

        /**
         * prominent peaks of the output (or input) spectrum, for Spectrum Grab
         */
        const std::vector<Peak>& getPeaks() const { return peaks_.getReader(); }

        /**
         * frequency regions where the output collides with the side-chain (or input), as a horizontal gradient
         */
        const juce::ColourGradient& getCollision() const { return collision_.getReader(); }

        bool isCollisionOn() const { return collision_on_; }

        /**
         * the analyzer freezes while the mouse is over the graph, if freeze is enabled
         */
        void setMouseOver(const bool over) { mouse_over_.store(over, std::memory_order::relaxed); }

    private:
        static constexpr size_t kNumResolutions = 3;
        static constexpr size_t kLow = 0, kMiddle = 1, kHigh = 2;

        PluginProcessor& p_ref_;
        MatchEngine* match_;
        std::atomic<float>& pre_ref_;
        std::atomic<float>& post_ref_;
        std::atomic<float>& side_ref_;
        std::atomic<float>& stereo_ref_;
        std::atomic<float>& freeze_ref_;
        std::atomic<float>& top_db_ref_;
        std::atomic<float>& min_db_ref_;
        std::atomic<float>& speed_ref_;
        std::atomic<float>& tilt_ref_;
        std::atomic<float>& smooth_oct_ref_;
        std::atomic<float>& smooth_erb_ref_;
        std::atomic<float>& smooth_type_ref_;
        std::atomic<float>& collision_ref_;
        std::atomic<float>& collision_strength_ref_;
        std::atomic<bool> mouse_over_{false};

        // geometry, written by the message thread
        std::atomic<float> bound_x_{0.f}, bound_y_{0.f}, bound_w_{1.f}, bound_h_{1.f};
        zlchore::thread::Notifier to_update_geometry_{true};
        std::atomic<float> refresh_rate_{60.f};
        zlchore::thread::Notifier to_update_decay_{true};

        // worker-thread state
        std::array<zldsp::analyzer::FFTAnalyzerProcessor, kNumResolutions> processors_;
        std::array<zldsp::analyzer::FFTAnalyzerReceiver, kNumSources> receivers_{
            zldsp::analyzer::FFTAnalyzerReceiver{processors_[kMiddle]},
            zldsp::analyzer::FFTAnalyzerReceiver{processors_[kMiddle]},
            zldsp::analyzer::FFTAnalyzerReceiver{processors_[kMiddle]}
        };
        std::array<zldsp::analyzer::SpectrumSmoother, kNumResolutions> smoothers_;
        zldsp::analyzer::SpectrumTilter tilter_;
        std::array<zldsp::analyzer::SpectrumDecayer, kNumSources> decayers_;
        std::array<std::array<zldsp::vector::aligned_vector<float>, kNumResolutions>, kNumSources> res_spectra_;
        std::array<zldsp::vector::aligned_vector<float>, kNumSources> spectra_;
        std::array<float, kNumResolutions> noise_power_scales_{};
        std::vector<float> frequencies_, xs_, ys_;
        size_t first_point_{0}, num_points_{0};

        double c_sample_rate_{0.0};
        int history_size_{0};
        float c_top_db_{1000.f}, c_range_db_{0.f};
        int c_speed_idx_{-1}, c_tilt_idx_{-1};
        int c_smooth_oct_idx_{-1}, c_smooth_erb_idx_{-1}, c_smooth_type_idx_{-1};
        float c_bottom_{0.f};
        float y_k_{0.f}, y_b_{0.f};
        FreqScale freq_scale_;

        std::array<TriBuffer<Paths>, kNumSources> paths_;
        TriBuffer<std::vector<Peak>> peaks_;
        TriBuffer<juce::ColourGradient> collision_;
        bool collision_on_{false};
        zldsp::vector::aligned_vector<float> collision_ps_, collision_final_ps_;
        std::array<bool, kNumSources> source_on_{};
        std::array<std::atomic<float>, 2> out_peaks_{};

        void run() override;

        void runOnce();

        void prepareResolution(double sample_rate);

        void findPeaks(const zldsp::vector::aligned_vector<float>& spectrum, std::vector<Peak>& peaks) const;

        void buildCollision(zldsp::vector::aligned_vector<float>& a, zldsp::vector::aligned_vector<float>& b);
    };
}
