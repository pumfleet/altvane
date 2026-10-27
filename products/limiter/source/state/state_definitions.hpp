// Copyright (C) 2026 - zsliu98
// This file is part of ZLLimiter
//
// ZLLimiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// ZLLimiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with ZLLimiter. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace zlstate {
    inline static constexpr int kVersionHint = 1;

    inline static constexpr size_t kBandNUM = 8;

    // float
    template <class T>
    class FloatParameters {
    public:
        static std::unique_ptr<juce::AudioParameterFloat> get(const bool automate = true) {
            auto attributes = juce::AudioParameterFloatAttributes().withAutomatable(automate).withLabel(T::kName);
            return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(T::kID, kVersionHint),
                                                               T::kName, T::kRange, T::kDefaultV, attributes);
        }

        static std::unique_ptr<juce::AudioParameterFloat> get(const std::string& suffix, const bool automate = true) {
            auto attributes = juce::AudioParameterFloatAttributes().withAutomatable(automate).withLabel(T::kName);
            return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(T::kID + suffix, kVersionHint),
                                                               T::kName + suffix, T::kRange, T::kDefaultV, attributes);
        }

        static std::unique_ptr<juce::AudioParameterFloat> get(const std::string& suffix, const bool meta,
                                                              const bool automate = true) {
            auto attributes = juce::AudioParameterFloatAttributes().withAutomatable(automate).withLabel(T::kName).
                withMeta(meta);
            return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(T::kID + suffix, kVersionHint),
                                                               T::kName + suffix, T::kRange, T::kDefaultV, attributes);
        }

        inline static float convertTo01(const float x) {
            return T::kRange.convertTo0to1(x);
        }
    };

    // bool
    template <class T>
    class BoolParameters {
    public:
        static std::unique_ptr<juce::AudioParameterBool> get(bool automate = true) {
            auto attributes = juce::AudioParameterBoolAttributes().withAutomatable(automate).withLabel(T::kName);
            return std::make_unique<juce::AudioParameterBool>(juce::ParameterID(T::kID, kVersionHint),
                                                              T::kName, T::kDefaultV, attributes);
        }

        static std::unique_ptr<juce::AudioParameterBool> get(const std::string& suffix, bool automate = true) {
            auto attributes = juce::AudioParameterBoolAttributes().withAutomatable(automate).withLabel(T::kName);
            return std::make_unique<juce::AudioParameterBool>(juce::ParameterID(T::kID + suffix, kVersionHint),
                                                              T::kName + suffix, T::kDefaultV, attributes);
        }

        static std::unique_ptr<juce::AudioParameterBool> get(const std::string& suffix, const bool meta,
                                                             const bool automate = true) {
            auto attributes = juce::AudioParameterBoolAttributes().withAutomatable(automate).withLabel(T::kName).
                withMeta(meta);
            return std::make_unique<juce::AudioParameterBool>(juce::ParameterID(T::kID + suffix, kVersionHint),
                                                              T::kName + suffix, T::kDefaultV, attributes);
        }

        inline static float convertTo01(const bool x) {
            return x ? 1.f : 0.f;
        }
    };

    // choice
    template <class T>
    class ChoiceParameters {
    public:
        static std::unique_ptr<juce::AudioParameterChoice> get(const bool automate = true) {
            auto attributes = juce::AudioParameterChoiceAttributes().withAutomatable(automate).withLabel(T::kName);
            return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(T::kID, kVersionHint),
                                                                T::kName, T::kChoices, T::kDefaultI, attributes);
        }

        static std::unique_ptr<juce::AudioParameterChoice> get(const std::string& suffix, const bool automate = true) {
            auto attributes = juce::AudioParameterChoiceAttributes().withAutomatable(automate).withLabel(T::kName);
            return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(T::kID + suffix, kVersionHint),
                                                                T::kName + suffix, T::kChoices, T::kDefaultI,
                                                                attributes);
        }

        static std::unique_ptr<juce::AudioParameterChoice> get(const std::string& suffix, const bool meta,
                                                               const bool automate = true) {
            auto attributes = juce::AudioParameterChoiceAttributes().withAutomatable(
                automate).withLabel(T::kName).withMeta(meta);
            return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(T::kID + suffix, kVersionHint),
                                                                T::kName + suffix, T::kChoices, T::kDefaultI,
                                                                attributes);
        }

        inline static float convertTo01(const int x) {
            return static_cast<float>(x) / static_cast<float>(T::kChoices.size() - 1);
        }
    };

    class PAnalyzerMagType : public ChoiceParameters<PAnalyzerMagType> {
    public:
        static constexpr auto kID = "analyzer_mag_type";
        static constexpr auto kName = "";
        inline static const auto kChoices = juce::StringArray{
            "Peak", "True Peak"
        };
        static constexpr int kDefaultI = 0;
    };

    class PAnalyzerMoveType : public ChoiceParameters<PAnalyzerMoveType> {
    public:
        static constexpr auto kID = "analyzer_move_type";
        static constexpr auto kName = "";
        inline static const auto kChoices = juce::StringArray{
            "Sync", "Slow", "Roll"
        };
        static constexpr int kDefaultI = 1;
    };

    class PAnalyzerMinDB : public ChoiceParameters<PAnalyzerMinDB> {
    public:
        static constexpr auto kID = "analyzer_min_db";
        static constexpr auto kName = "";
        inline static const auto kChoices = juce::StringArray{
            "-6", "-12", "-24", "-36", "-54"
        };
        static constexpr std::array kDBs = {-6.f, -12.f, -24.f, -36.f, -54.f};
        static constexpr int kDefaultI = 3;

        static constexpr float getDBFromIndex(const float x) {
            return kDBs[static_cast<size_t>(std::round(x))];
        }
    };

    class PAnalyzerTimeLength : public ChoiceParameters<PAnalyzerTimeLength> {
    public:
        static constexpr auto kID = "analyzer_time_length";
        static constexpr auto kName = "";
        inline static const auto kChoices = juce::StringArray{
            "6 s", "9 s", "12 s", "18 s"
        };
        static constexpr std::array kLength = {6.f, 9.f, 12.f, 18.f};
        static constexpr int kDefaultI = 1;
    };

    class PPreCurveDisplay : public BoolParameters<PPreCurveDisplay> {
    public:
        static constexpr auto kID = "pre_curve_display";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PPostCurveDisplay : public BoolParameters<PPostCurveDisplay> {
    public:
        static constexpr auto kID = "post_curve_display";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PDeltaCurveDisplay : public BoolParameters<PDeltaCurveDisplay> {
    public:
        static constexpr auto kID = "delta_curve_display";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PMeterDisplayON : public BoolParameters<PMeterDisplayON> {
    public:
        static constexpr auto kID = "meter_display_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueDisplayON : public BoolParameters<PValueDisplayON> {
    public:
        static constexpr auto kID = "value_display_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueTruePeakON : public BoolParameters<PValueTruePeakON> {
    public:
        static constexpr auto kID = "value_true_peak_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueLUFSSON : public BoolParameters<PValueLUFSSON> {
    public:
        static constexpr auto kID = "value_lufs_s_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueLUFSION : public BoolParameters<PValueLUFSION> {
    public:
        static constexpr auto kID = "value_lufs_i_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueLRAON : public BoolParameters<PValueLRAON> {
    public:
        static constexpr auto kID = "value_lra_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueLUFSMON : public BoolParameters<PValueLUFSMON> {
    public:
        static constexpr auto kID = "value_lufs_m_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueStereoCorrON : public BoolParameters<PValueStereoCorrON> {
    public:
        static constexpr auto kID = "value_stereo_corr_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PValueHistogramON : public BoolParameters<PValueHistogramON> {
    public:
        static constexpr auto kID = "value_histogram_on";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };


    // ---- Altvane: loudness targets and the editor's display (non-automatable, saved with the session) ----

    class PLoudnessTarget : public ChoiceParameters<PLoudnessTarget> {
    public:
        static constexpr auto kID = "altvane_loudness_target";
        static constexpr auto kName = "";
        // the order matches altvane::limiter::kLoudnessTargets
        inline static const auto kChoices = juce::StringArray{
            "Streaming", "Apple Music", "YouTube", "EBU R128", "ATSC A/85", "Podcast", "CD / Club", "Custom"
        };
        static constexpr int kDefaultI = 0;
    };

    class PCustomTargetLUFS : public FloatParameters<PCustomTargetLUFS> {
    public:
        static constexpr auto kID = "altvane_custom_target_lufs";
        static constexpr auto kName = "";
        inline static const auto kRange = juce::NormalisableRange<float>(-36.f, -5.f, .1f);
        static constexpr auto kDefaultV = -14.f;
    };

    class PCustomTargetTruePeak : public FloatParameters<PCustomTargetTruePeak> {
    public:
        static constexpr auto kID = "altvane_custom_target_true_peak";
        static constexpr auto kName = "";
        inline static const auto kRange = juce::NormalisableRange<float>(-6.f, 0.f, .1f);
        static constexpr auto kDefaultV = -1.f;
    };

    class PLoudnessScale : public ChoiceParameters<PLoudnessScale> {
    public:
        static constexpr auto kID = "altvane_loudness_scale";
        static constexpr auto kName = "";
        // LUFS: absolute; LU: relative to the target; K-12/14/20: 0 at -12/-14/-20 LUFS
        inline static const auto kChoices = juce::StringArray{"LUFS", "LU", "K-12", "K-14", "K-20"};
        static constexpr int kDefaultI = 0;
    };

    class PLoudnessAutoReset : public BoolParameters<PLoudnessAutoReset> {
    public:
        static constexpr auto kID = "altvane_loudness_auto_reset";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PLoudnessPanelOpen : public BoolParameters<PLoudnessPanelOpen> {
    public:
        static constexpr auto kID = "altvane_loudness_panel_open";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    class PControlsOpen : public BoolParameters<PControlsOpen> {
    public:
        static constexpr auto kID = "altvane_controls_open";
        static constexpr auto kName = "";
        static constexpr auto kDefaultV = true;
    };

    inline juce::AudioProcessorValueTreeState::ParameterLayout getNAParameterLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add(PAnalyzerMagType::get(false), PAnalyzerMoveType::get(false),
                   PAnalyzerMinDB::get(false), PAnalyzerTimeLength::get(false),
                   PPreCurveDisplay::get(false), PPostCurveDisplay::get(false), PDeltaCurveDisplay::get(false),
                   PMeterDisplayON::get(false), PValueDisplayON::get(false),
                   PValueTruePeakON::get(false), PValueStereoCorrON::get(false), PValueLUFSMON::get(false),
                   PValueLUFSSON::get(false), PValueLRAON::get(false), PValueLUFSION::get(false),
                   PValueHistogramON::get(false));
        // Altvane
        layout.add(PLoudnessTarget::get(false), PCustomTargetLUFS::get(false), PCustomTargetTruePeak::get(false),
                   PLoudnessScale::get(false), PLoudnessAutoReset::get(false),
                   PLoudnessPanelOpen::get(false), PControlsOpen::get(false));
        return layout;
    }
}
