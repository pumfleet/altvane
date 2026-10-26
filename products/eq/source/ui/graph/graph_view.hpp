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
#include "altvane_ui/util/freq_scale.hpp"
#include "altvane_ui/util/spring.hpp"
#include "../model/engine_bridge.hpp"
#include "../model/selection.hpp"
#include "../analysis/response_model.hpp"
#include "../analysis/spectrum_worker.hpp"

#include <optional>

namespace altvane::eq {
    /**
     * the full-bleed frequency graph: grid, analyzer, band and composite curves, nodes and all their interactions
     */
    class GraphView final : public juce::Component,
                            private juce::Timer {
    public:
        GraphView(EngineBridge& bridge, Selection& selection, ResponseModel& response, SpectrumWorker& spectrum);

        ~GraphView() override;

        /**
         * advance one frame; the bridge has already been refreshed
         */
        void tick(double dt);

        void paint(juce::Graphics& g) override;

        void resized() override;

        /**
         * x of a band's node in local coordinates
         */
        float getNodeX(size_t band) const;

        /**
         * momentary band solo through the engine's solo filter; kBandNum turns it off
         */
        void setSolo(size_t band, bool side_chain = false);

        size_t getSoloBand() const { return solo_band_; }

        bool isSoloSideChain() const { return solo_side_; }

        void setBottomInset(float inset);

        /**
         * paint-time overlay for performance work (Cmd+Alt+F)
         */
        void togglePerformanceOverlay() { show_perf_ = !show_perf_; repaint(); }

        /**
         * click a spectrum peak to create a band there and drag it
         */
        void setSpectrumGrabEnabled(const bool enabled) { grab_enabled_ = enabled; }

        /**
         * while EQ Match is active the graph shows its curves and edits the correction instead of bands
         */
        void setMatchEngine(MatchEngine* engine) { match_ = engine; }

        /**
         * place a virtual pointer over the graph (snapshot tool, tests)
         */
        void hoverForTesting(const juce::Point<float> p) {
            mouse_inside_ = true;
            mouse_pos_ = p;
            updateHover(p);
        }

        // undo / redo shared with the header
        void undo();

        void redo();

    private:
        enum class DragMode { kNone, kPending, kNode, kQLeft, kQRight, kTarget, kSide, kSideLeft, kSideRight, kLasso };

        enum class HitType { kNone, kNode, kQLeft, kQRight, kTarget, kSide, kSideLeft, kSideRight, kPeak };

        struct Hit {
            HitType type{HitType::kNone};
            size_t band{kBandNum};
        };

        struct DragStart {
            size_t band;
            float freq, node_db, q, target_gain, gain;
        };

        EngineBridge& bridge_;
        Selection& selection_;
        ResponseModel& response_;
        SpectrumWorker& spectrum_;

        FreqScale freq_scale_;
        GainScale gain_scale_;
        float bottom_inset_{0.f};

        // per band animation
        std::array<Spring, kBandNum> hover_springs_;
        std::array<Spring, kBandNum> select_springs_;
        std::array<Spring, kBandNum> appear_springs_;
        Fade readout_fade_{.12f};
        Fade empty_fade_{.3f};
        Spring solo_spring_{0.f, 5.f};
        size_t readout_band_{kBandNum};
        float readout_delay_{0.f};

        // cached geometry
        std::array<juce::Path, kBandNum> band_fill_paths_, band_line_paths_;
        std::array<juce::Path, kBandNum> range_paths_;
        std::array<juce::Path, ResponseModel::kNumStereo> sum_paths_;
        bool curves_dirty_{true};
        juce::Image grid_image_;
        float grid_scale_{0.f};
        size_t grid_db_idx_{100};
        bool grid_piano_{false};
        float grid_spectrum_top_{1000.f};

        // interaction
        DragMode drag_mode_{DragMode::kNone};
        Hit hover_hit_;
        juce::Point<float> mouse_pos_{-1.f, -1.f};
        bool mouse_inside_{false};
        juce::Point<float> drag_start_pos_, drag_virtual_pos_, drag_last_pos_;
        std::vector<DragStart> drag_starts_;
        size_t drag_band_{kBandNum};
        int axis_lock_{0}; // 0 undecided, 1 horizontal, 2 vertical
        std::bitset<kBandNum> lasso_initial_;
        juce::Rectangle<float> lasso_rect_;
        bool wheel_editing_{false};
        size_t solo_band_{kBandNum};
        bool solo_side_{false};
        bool key_solo_{false};
        std::uint32_t last_selection_version_{0};
        bool show_perf_{false};
        bool grab_enabled_{true};
        std::optional<SpectrumWorker::Peak> grab_peak_;
        Fade grab_fade_{.12f};
        bool piano_on_{false};
        MatchEngine* match_{nullptr};
        bool match_drawing_{false}, match_erasing_{false};
        size_t match_last_idx_{0};
        float match_last_db_{0.f};
        juce::Path match_source_, match_target_, match_correction_, match_fill_;
        double paint_ms_avg_{0.0}, paint_ms_max_{0.0};

        // geometry helpers
        juce::Point<float> getNodePosition(size_t band) const;
        juce::Point<float> getTargetPosition(size_t band) const;
        std::pair<float, float> getQHandleXs(size_t band) const;
        bool hasQHandles(size_t band) const;

        struct SideRange {
            float left, centre, right;
            bool has_centre, has_edges;
        };

        SideRange getSideRange(size_t band) const;
        bool showsSideChain(size_t band) const;
        static constexpr float kSideBarY = 18.f;
        float getMaxDB() const;

        Hit hitTestNodes(juce::Point<float> p) const;
        void updateHover(juce::Point<float> p);

        void rebuildCurves();
        void rebuildGrid(float scale);

        // painting
        void paintSpectrum(juce::Graphics& g);
        void paintCurves(juce::Graphics& g);
        void paintNodes(juce::Graphics& g);
        void paintReadout(juce::Graphics& g);
        void paintCursor(juce::Graphics& g);
        void paintSolo(juce::Graphics& g);
        void paintEmptyState(juce::Graphics& g);
        void paintSideChain(juce::Graphics& g);
        void paintGrab(juce::Graphics& g);
        void paintPiano(juce::Graphics& g);
        void paintMatch(juce::Graphics& g);
        bool isMatching() const { return match_ != nullptr && match_->isActive(); }
        size_t matchIndexAt(float x) const;
        void matchDraw(juce::Point<float> p, bool start);
        std::optional<SpectrumWorker::Peak> findGrabPeak(juce::Point<float> p) const;

        // editing
        void beginNodeDrag(size_t band);
        void dragNodes(juce::Point<float> delta, bool constrain);
        void dragQ(float x, bool left);
        void dragTarget(float dy);
        void dragSide(float x, DragMode mode);
        void showBandMenu(size_t band);
        void showAddMenu(juce::Point<float> p);
        void addBandAt(juce::Point<float> p, zldsp::filter::FilterType type);
        std::vector<size_t> getSelectedBands() const;
        void nudge(float freq_octaves, float gain_db);
        void cycleSelection(bool forwards);
        void selectAdded(const std::vector<size_t>& bands);
        void updateDescription();

        void mouseMove(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        bool keyPressed(const juce::KeyPress& key) override;
        bool keyStateChanged(bool is_key_down) override;

        void timerCallback() override;
    };
}
