// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include "engine_bridge.hpp"

namespace altvane::eq {
    namespace {
        constexpr std::array<const char*, BandParams::kNumFields> kBandIDs{
            zlp::PFilterStatus::kID, zlp::PFilterType::kID, zlp::POrder::kID, zlp::PLRMode::kID,
            zlp::PFreq::kID, zlp::PGain::kID, zlp::PTargetGain::kID, zlp::PQ::kID,
            zlp::PDynamicON::kID, zlp::PDynamicLearn::kID, zlp::PDynamicBypass::kID, zlp::PDynamicRelative::kID,
            zlp::PSideSwap::kID, zlp::PSideLink::kID, zlp::PThreshold::kID, zlp::PKneeW::kID,
            zlp::PAttack::kID, zlp::PRelease::kID,
            zlp::PDynamicRMSLength::kID, zlp::PDynamicRMSMix::kID, zlp::PDynamicSmooth::kID,
            zlp::PSideFilterType::kID, zlp::PSideOrder::kID, zlp::PSideFreq::kID, zlp::PSideQ::kID
        };

        constexpr std::array<const char*, 10> kGlobalIDs{
            zlp::PFilterStructure::kID, zlp::PExtSide::kID, zlp::PBypass::kID, zlp::POutputGain::kID,
            zlp::PGainScale::kID, zlp::PAutoGain::kID, zlp::PStaticGain::kID, zlp::PPhaseFlip::kID,
            zlp::PLookahead::kID, zlp::POutputStereo::kID
        };

        constexpr std::array<const char*, 9> kNAIDs{
            zlstate::PEQMaxDB::kID, zlstate::PFFTPreON::kID, zlstate::PFFTPostON::kID, zlstate::PFFTSideON::kID,
            zlstate::PFFTFreezeON::kID, zlstate::PFFTSpeed::kID, zlstate::PFFTTilt::kID,
            zlstate::PFFTTopDB::kID, zlstate::PFFTMinDB::kID
        };

        size_t toIndex(const float v) {
            return static_cast<size_t>(std::max(0, juce::roundToInt(v)));
        }
    }

    EngineBridge::EngineBridge(PluginProcessor& p) :
        EngineBridge(p, *new ParamEditor(p.session_.undo_manager), true) {
    }

    EngineBridge::EngineBridge(PluginProcessor& p, ParamEditor& editor) : EngineBridge(p, editor, false) {
    }

    EngineBridge::EngineBridge(PluginProcessor& p, ParamEditor& editor, const bool owns_editor) :
        p_ref_(p), own_editor_(owns_editor ? &editor : nullptr), editor_(editor) {
        for (size_t band = 0; band < kBandNum; ++band) {
            const auto suffix = std::to_string(band);
            for (size_t f = 0; f < BandParams::kNumFields; ++f) {
                const auto id = kBandIDs[f] + suffix;
                band_params_[band].paras[f] = p_ref_.parameters_.getParameter(id);
                band_params_[band].raws[f] = p_ref_.parameters_.getRawParameterValue(id);
                jassert(band_params_[band].paras[f] != nullptr);
                p_ref_.parameters_.addParameterListener(id, this);
            }
            band_dirty_[band].signal();
        }
        for (const auto* id : kGlobalIDs) {
            p_ref_.parameters_.addParameterListener(id, this);
        }
        for (const auto* id : kNAIDs) {
            p_ref_.parameters_NA_.addParameterListener(id, this);
        }
        refresh();
    }

    EngineBridge::~EngineBridge() {
        if (isEditing()) {
            endEdit();
        }
        for (size_t band = 0; band < kBandNum; ++band) {
            const auto suffix = std::to_string(band);
            for (const auto* id : kBandIDs) {
                p_ref_.parameters_.removeParameterListener(id + suffix, this);
            }
        }
        for (const auto* id : kGlobalIDs) {
            p_ref_.parameters_.removeParameterListener(id, this);
        }
        for (const auto* id : kNAIDs) {
            p_ref_.parameters_NA_.removeParameterListener(id, this);
        }
    }

    void EngineBridge::parameterChanged(const juce::String& parameter_id, float) {
        // may be called from any thread, only raise flags here
        if (parameter_id.startsWith("total_") || !juce::CharacterFunctions::isDigit(parameter_id.getLastCharacter())) {
            global_dirty_.signal();
            return;
        }
        const auto band = static_cast<size_t>(parameter_id.getTrailingIntValue());
        if (band < kBandNum) {
            band_dirty_[band].signal();
            any_band_dirty_.signal();
        }
    }

    bool EngineBridge::refresh() {
        if (any_band_dirty_.check()) {
            for (size_t band = 0; band < kBandNum; ++band) {
                if (band_dirty_[band].check()) {
                    readBand(band);
                    changed_bands_.set(band);
                }
            }
        }
        if (global_dirty_.check()) {
            readGlobal();
            globals_changed_ = true;
        }
        if (first_refresh_) {
            first_refresh_ = false;
            changed_bands_.set();
            globals_changed_ = true;
        }
        return changed_bands_.any() || globals_changed_;
    }

    void EngineBridge::readBand(const size_t idx) {
        const auto& bp = band_params_[idx];
        auto& b = bands_[idx];
        b.status = static_cast<zlp::FilterStatus>(toIndex(bp.raw(BandParams::kStatus)));
        b.type = static_cast<zldsp::filter::FilterType>(toIndex(bp.raw(BandParams::kType)));
        b.order_idx = std::min<size_t>(toIndex(bp.raw(BandParams::kOrder)), zlp::POrder::kOrderArray.size() - 1);
        b.lr = static_cast<zlp::FilterStereo>(toIndex(bp.raw(BandParams::kLR)));
        b.freq = bp.raw(BandParams::kFreq);
        b.gain = bp.raw(BandParams::kGain);
        b.target_gain = bp.raw(BandParams::kTargetGain);
        b.q = bp.raw(BandParams::kQ);
        b.dynamic_on = bp.raw(BandParams::kDynamicOn) > .5f;
        b.dynamic_learn = bp.raw(BandParams::kDynamicLearn) > .5f;
        b.dynamic_bypass = bp.raw(BandParams::kDynamicBypass) > .5f;
        b.dynamic_relative = bp.raw(BandParams::kDynamicRelative) > .5f;
        b.side_swap = bp.raw(BandParams::kSideSwap) > .5f;
        b.side_link = bp.raw(BandParams::kSideLink) > .5f;
        b.threshold = bp.raw(BandParams::kThreshold);
        b.knee = bp.raw(BandParams::kKnee);
        b.attack = bp.raw(BandParams::kAttack);
        b.release = bp.raw(BandParams::kRelease);
        b.side_type = toIndex(bp.raw(BandParams::kSideType));
        b.side_order_idx = std::min<size_t>(toIndex(bp.raw(BandParams::kSideOrder)),
                                            zlp::PSideOrder::kOrderArray.size() - 1);
        b.side_freq = bp.raw(BandParams::kSideFreq);
        b.side_q = bp.raw(BandParams::kSideQ);
    }

    float EngineBridge::getRaw(const juce::AudioProcessorValueTreeState& tree, const char* id) const {
        if (const auto* v = tree.getRawParameterValue(id)) {
            return v->load(std::memory_order::relaxed);
        }
        return 0.f;
    }

    void EngineBridge::readGlobal() {
        const auto& t = p_ref_.parameters_;
        global_.structure = static_cast<zlp::FilterStructure>(toIndex(getRaw(t, zlp::PFilterStructure::kID)));
        global_.ext_side = getRaw(t, zlp::PExtSide::kID) > .5f;
        global_.bypass = getRaw(t, zlp::PBypass::kID) > .5f;
        global_.output_gain = getRaw(t, zlp::POutputGain::kID);
        global_.gain_scale = getRaw(t, zlp::PGainScale::kID);
        global_.auto_gain = getRaw(t, zlp::PAutoGain::kID) > .5f;
        global_.static_gain = getRaw(t, zlp::PStaticGain::kID) > .5f;
        global_.phase_flip = getRaw(t, zlp::PPhaseFlip::kID) > .5f;
        global_.lookahead = getRaw(t, zlp::PLookahead::kID);
        global_.output_stereo = static_cast<zlp::FilterStereo>(toIndex(getRaw(t, zlp::POutputStereo::kID)));
        const auto& na = p_ref_.parameters_NA_;
        global_.eq_max_db_idx = std::min<size_t>(toIndex(getRaw(na, zlstate::PEQMaxDB::kID)),
                                                 zlstate::PEQMaxDB::kDBs.size() - 1);
        global_.fft_pre = getRaw(na, zlstate::PFFTPreON::kID) > .5f;
        global_.fft_post = getRaw(na, zlstate::PFFTPostON::kID) > .5f;
        global_.fft_side = getRaw(na, zlstate::PFFTSideON::kID) > .5f;
        global_.fft_frozen = getRaw(na, zlstate::PFFTFreezeON::kID) > .5f;
        global_.fft_speed_idx = toIndex(getRaw(na, zlstate::PFFTSpeed::kID));
        global_.fft_tilt_idx = toIndex(getRaw(na, zlstate::PFFTTilt::kID));
        global_.fft_top_db_idx = std::min<size_t>(toIndex(getRaw(na, zlstate::PFFTTopDB::kID)),
                                                  zlstate::PFFTTopDB::kDBs.size() - 1);
        global_.fft_min_db_idx = std::min<size_t>(toIndex(getRaw(na, zlstate::PFFTMinDB::kID)),
                                                  zlstate::PFFTMinDB::kDBs.size() - 1);
    }

    juce::RangedAudioParameter* EngineBridge::globalParam(const juce::String& id) const {
        return p_ref_.parameters_.getParameter(id);
    }

    juce::RangedAudioParameter* EngineBridge::naParam(const juce::String& id) const {
        return p_ref_.parameters_NA_.getParameter(id);
    }

    void EngineBridge::setNA(const juce::String& id, const float plain) {
        if (auto* para = naParam(id)) {
            zlp::updateParaNotifyHost(para, para->convertTo0to1(plain));
        }
    }

    size_t EngineBridge::findOffBand() const {
        for (size_t band = 0; band < kBandNum; ++band) {
            if (band_params_[band].raw(BandParams::kStatus) < .5f) {
                return band;
            }
        }
        return kBandNum;
    }
}
