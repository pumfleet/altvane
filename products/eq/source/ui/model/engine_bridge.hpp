// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <bitset>
#include <span>

#include "../common.hpp"
#include "../../PluginProcessor.hpp"
#include "../../chore/thread/notifier.hpp"

namespace altvane::eq {
    inline constexpr size_t kBandNum = zlp::kBandNum;

    /**
     * every per-band parameter, resolved once
     */
    struct BandParams {
        enum Field {
            kStatus, kType, kOrder, kLR, kFreq, kGain, kTargetGain, kQ,
            kDynamicOn, kDynamicLearn, kDynamicBypass, kDynamicRelative,
            kSideSwap, kSideLink, kThreshold, kKnee, kAttack, kRelease,
            kRMSLength, kRMSMix, kSmooth, kSideType, kSideOrder, kSideFreq, kSideQ,
            kNumFields
        };

        std::array<juce::RangedAudioParameter*, kNumFields> paras{};
        std::array<std::atomic<float>*, kNumFields> raws{};

        juce::RangedAudioParameter* operator[](const Field f) const { return paras[static_cast<size_t>(f)]; }

        float raw(const Field f) const { return raws[static_cast<size_t>(f)]->load(std::memory_order::relaxed); }
    };

    /**
     * a message-thread copy of one band
     */
    struct BandSnapshot {
        zlp::FilterStatus status{zlp::kOff};
        zldsp::filter::FilterType type{zldsp::filter::kPeak};
        size_t order_idx{1};
        zlp::FilterStereo lr{zlp::kStereo};
        float freq{1000.f}, gain{0.f}, target_gain{0.f}, q{.707f};
        bool dynamic_on{false}, dynamic_learn{false}, dynamic_bypass{false}, dynamic_relative{false};
        bool side_swap{false}, side_link{false};
        float threshold{-40.f}, knee{8.f}, attack{100.f}, release{500.f};
        size_t side_type{0}, side_order_idx{1};
        float side_freq{1000.f}, side_q{.707f};

        bool isActive() const { return status != zlp::kOff; }
        bool isOn() const { return status == zlp::kOn; }
        size_t getOrder() const { return zlp::POrder::kOrderArray[order_idx]; }

        bool hasGain() const {
            using namespace zldsp::filter;
            return type == kPeak || type == kLowShelf || type == kHighShelf
                || type == kTiltShelf || type == kFlatTilt || type == kFlatGain;
        }

        bool hasQ() const {
            using namespace zldsp::filter;
            if (type == kFlatGain) {
                return false;
            }
            return order_idx > 0 || type == kPeak || type == kNotch || type == kBandPass;
        }

        bool hasSlope() const {
            return type != zldsp::filter::kFlatGain;
        }
    };

    struct GlobalSnapshot {
        zlp::FilterStructure structure{zlp::kMinimum};
        bool ext_side{false}, bypass{false};
        float output_gain{0.f}, gain_scale{100.f};
        bool auto_gain{false}, static_gain{false}, phase_flip{false};
        float lookahead{0.f};
        zlp::FilterStereo output_stereo{zlp::kStereo};
        // non-automatable, saved with the session
        size_t eq_max_db_idx{1};
        bool fft_pre{true}, fft_post{true}, fft_side{false}, fft_frozen{false};
        size_t fft_speed_idx{2}, fft_tilt_idx{3};
        size_t fft_top_db_idx{1}, fft_min_db_idx{0};
    };

    /**
     * the only object in the new UI that talks to the processor
     * parameter listeners only raise flags; refresh() copies values on the message thread
     */
    class EngineBridge final : private juce::AudioProcessorValueTreeState::Listener {
    public:
        explicit EngineBridge(PluginProcessor& p);

        ~EngineBridge() override;

        /**
         * copy changed parameters into the snapshots; may be called any number of times,
         * the changed flags accumulate until clearChanges()
         * @return whether anything changed
         */
        bool refresh();

        /**
         * call at the end of a frame, after every consumer has seen the changes
         */
        void clearChanges() {
            changed_bands_.reset();
            globals_changed_ = false;
        }

        const BandSnapshot& band(const size_t idx) const { return bands_[idx]; }
        const GlobalSnapshot& global() const { return global_; }
        const std::bitset<kBandNum>& getChangedBands() const { return changed_bands_; }
        bool haveGlobalsChanged() const { return globals_changed_; }

        const BandParams& params(const size_t idx) const { return band_params_[idx]; }
        juce::RangedAudioParameter* globalParam(const juce::String& id) const;
        juce::RangedAudioParameter* naParam(const juce::String& id) const;

        /**
         * begin a continuous edit (drag, wheel), the host sees one gesture per parameter
         * and the undo history gets a single transaction when the edit ends
         */
        void beginEdit(std::span<juce::RangedAudioParameter* const> paras, const juce::String& name);

        void setPlain(juce::RangedAudioParameter* para, float plain);

        void endEdit();

        /**
         * end the current edit and restore every parameter it touched, leaving no undo step
         */
        void cancelEdit();

        bool isEditing() const { return !edit_.entries.empty(); }

        /**
         * set several parameters at once as one undoable step
         */
        void setNow(std::initializer_list<std::pair<juce::RangedAudioParameter*, float>> values,
                    const juce::String& name);

        void setNow(const std::vector<std::pair<juce::RangedAudioParameter*, float>>& values,
                    const juce::String& name);

        /**
         * non-automatable UI state is not part of the undo history
         */
        void setNA(const juce::String& id, float plain);

        juce::UndoManager& getUndoManager() { return p_ref_.undo_manager_; }
        zlp::Controller& getController() { return p_ref_.getController(); }
        PluginProcessor& getProcessor() { return p_ref_; }
        const PluginProcessor& getProcessor() const { return p_ref_; }
        double getSampleRate() const { return p_ref_.getAtomicSampleRate(); }

        size_t findOffBand() const;

    private:
        struct EditEntry {
            juce::RangedAudioParameter* para;
            float from;
        };

        struct Edit {
            juce::String name;
            std::vector<EditEntry> entries;
        };

        PluginProcessor& p_ref_;
        std::array<BandParams, kBandNum> band_params_;
        std::array<BandSnapshot, kBandNum> bands_;
        GlobalSnapshot global_;

        std::array<zlchore::thread::Notifier, kBandNum> band_dirty_;
        zlchore::thread::Notifier any_band_dirty_{true};
        zlchore::thread::Notifier global_dirty_{true};
        std::bitset<kBandNum> changed_bands_;
        bool globals_changed_{false};
        bool first_refresh_{true};

        Edit edit_;

        void parameterChanged(const juce::String& parameter_id, float new_value) override;

        void readBand(size_t idx);

        void readGlobal();

        float getRaw(const juce::AudioProcessorValueTreeState& tree, const char* id) const;
    };
}
