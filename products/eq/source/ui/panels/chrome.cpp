// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane EQ
//
// Altvane EQ is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane EQ is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane EQ. If not, see <https://www.gnu.org/licenses/>.
#include "chrome.hpp"

#include "altvane_ui/theme/fonts.hpp"
#include "altvane_ui/theme/look_and_feel.hpp"
#include "altvane_ui/util/value_format.hpp"
#include "../model/band_ops.hpp"
#include "../theme/eq_tokens.hpp"

namespace altvane::eq {
    using namespace tokens;

    namespace {
        const std::array<const char*, 6> kPhaseNames{"Minimum", "SVF", "Parallel", "Matched", "Mixed", "Linear"};
        const std::array<const char*, 6> kPhaseDescriptions{
            "Zero latency", "Zero latency, smooth automation", "Zero latency, natural dynamics",
            "Analog phase, latency", "Clean high end, latency", "No phase shift, latency"
        };

        void hairline(juce::Graphics& g, const float y, const float w) {
            g.setColour(colour::kHairline);
            g.fillRect(juce::Rectangle<float>(0.f, y, w, 1.f));
        }
    }

    // ---------------------------------------------------------------- Footer

    Footer::Footer(EngineBridge& bridge) : bridge_(bridge) {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &phase_, &pre_, &post_, &side_spectrum_, &analyzer_menu_, &match_, &ext_side_, &auto_gain_,
                 &scale_, &output_, &output_menu_, &bypass_
             }) {
            addAndMakeVisible(c);
        }
        phase_.setLabel("Processing");
        phase_.build_menu = [this] {
            juce::PopupMenu menu;
            menu.addSectionHeader("Processing");
            for (int i = 0; i < 6; ++i) {
                if (i == 3) {
                    menu.addSeparator();
                }
                menu.addItem(i + 1, juce::String(kPhaseNames[static_cast<size_t>(i)]) + "\t"
                             + kPhaseDescriptions[static_cast<size_t>(i)], true,
                             static_cast<int>(bridge_.global().structure) == i);
            }
            return menu;
        };
        phase_.on_result = [this](const int r) {
            bridge_.setNow({{bridge_.globalParam(zlp::PFilterStructure::kID), static_cast<float>(r - 1)}},
                           "Change Processing Mode");
        };

        pre_.setTooltip("Show the input spectrum");
        post_.setTooltip("Show the output spectrum");
        side_spectrum_.setTooltip("Show the side-chain spectrum");
        pre_.on_click = [this] { bridge_.setNA(zlstate::PFFTPreON::kID, bridge_.global().fft_pre ? 0.f : 1.f); };
        post_.on_click = [this] { bridge_.setNA(zlstate::PFFTPostON::kID, bridge_.global().fft_post ? 0.f : 1.f); };
        side_spectrum_.on_click = [this] {
            bridge_.setNA(zlstate::PFFTSideON::kID, bridge_.global().fft_side ? 0.f : 1.f);
        };
        pre_.setOnColour(colour::kTextPrimary);
        post_.setOnColour(colour::kTextPrimary);
        side_spectrum_.setOnColour(colour::kSpectrumSide);
        match_.setTooltip("Match the input to a reference spectrum");
        match_.on_click = [this] {
            if (on_toggle_match) {
                on_toggle_match();
            }
        };
        analyzer_menu_.setTooltip("Analyzer settings");
        analyzer_menu_.on_click = [this] { showAnalyzerMenu(); };

        ext_side_.setTooltip("Use the external side-chain input for dynamic bands");
        ext_side_.on_click = [this] {
            bridge_.setNow({{bridge_.globalParam(zlp::PExtSide::kID), bridge_.global().ext_side ? 0.f : 1.f}},
                           "Side-chain");
        };
        auto_gain_.setTooltip("Compensate the loudness change of the EQ");
        auto_gain_.on_click = [this] {
            bridge_.setNow({{bridge_.globalParam(zlp::PAutoGain::kID), bridge_.global().auto_gain ? 0.f : 1.f}},
                           "Auto Gain");
        };

        bindValue(scale_, bridge_.getParamEditor(), bridge_.globalParam(zlp::PGainScale::kID), "Scale");
        scale_.format = [](const float v) { return format::percent(v); };
        bindValue(output_, bridge_.getParamEditor(), bridge_.globalParam(zlp::POutputGain::kID), "Output");
        output_.format = [](const float v) { return format::gain(v); };

        output_menu_.setTooltip("Output options");
        output_menu_.on_click = [this] { showOutputMenu(); };

        bypass_.setToggleable(true);
        phase_.setComponentID(zlp::PFilterStructure::kID);
        bypass_.setComponentID(zlp::PBypass::kID);
        bypass_.setTooltip("Bypass the plug-in");
        bypass_.setOnColour(colour::kWarning);
        bypass_.on_click = [this] {
            bridge_.setNow({{bridge_.globalParam(zlp::PBypass::kID), bridge_.global().bypass ? 0.f : 1.f}},
                           "Bypass");
        };
        tick();
    }

    Footer::~Footer() {
        if (loudness_learning_) {
            bridge_.getController().setLoudnessMatchON(false);
        }
    }

    void Footer::setLoudnessLearning(const bool on) {
        if (on == loudness_learning_) {
            return;
        }
        loudness_learning_ = on;
        auto& controller = bridge_.getController();
        controller.setLoudnessMatchON(on);
        if (!on) {
            // the measured loudness difference becomes the output gain; auto gain would fight it
            const auto diff = static_cast<float>(controller.getLUFSMatcherDiff());
            bridge_.setNow({
                               {bridge_.globalParam(zlp::POutputGain::kID), std::clamp(-diff, -30.f, 30.f)},
                               {bridge_.globalParam(zlp::PAutoGain::kID), 0.f},
                           }, "Match Loudness");
        }
        output_.setLabel(on ? "Matching" : "Output");
        output_.setAccent(on ? colour::kAccent : colour::kTextTertiary);
        repaint();
    }

    void Footer::showOutputMenu() {
        const auto& g = bridge_.global();
        juce::PopupMenu channel, lookahead, menu;
        for (int i = 0; i < 5; ++i) {
            channel.addItem(100 + i, zlp::POutputStereo::kChoices[i], true, static_cast<int>(g.output_stereo) == i);
        }
        static constexpr std::array<float, 6> kLookaheads{0.f, 1.f, 2.f, 5.f, 10.f, 20.f};
        for (size_t i = 0; i < kLookaheads.size(); ++i) {
            lookahead.addItem(200 + static_cast<int>(i),
                              kLookaheads[i] < .5f ? juce::String("Off") : juce::String(kLookaheads[i], 0) + " ms",
                              true, std::abs(g.lookahead - kLookaheads[i]) < .05f);
        }
        menu.addSectionHeader("Output");
        menu.addItem(1, "Static Gain Compensation", true, g.static_gain);
        menu.addItem(2, "Invert Phase", true, g.phase_flip);
        menu.addSubMenu("Channel", channel);
        menu.addSubMenu("Side-chain Lookahead", lookahead);
        menu.addSeparator();
        menu.addItem(3, loudness_learning_ ? "Apply Loudness Match" : "Learn Loudness Match...");
        if (loudness_learning_) {
            menu.addItem(4, "Cancel Loudness Match");
        }
        showPopupMenu(menu, *this, juce::PopupMenu::Options().withTargetComponent(&output_menu_),
                           [safe = juce::Component::SafePointer(this)](const int r) {
                               if (safe == nullptr || r == 0) {
                                   return;
                               }
                               auto& b = safe->bridge_;
                               const auto& gl = b.global();
                               if (r == 1) {
                                   b.setNow({{b.globalParam(zlp::PStaticGain::kID), gl.static_gain ? 0.f : 1.f}},
                                            "Static Gain Compensation");
                               } else if (r == 2) {
                                   b.setNow({{b.globalParam(zlp::PPhaseFlip::kID), gl.phase_flip ? 0.f : 1.f}},
                                            "Invert Phase");
                               } else if (r == 3) {
                                   safe->setLoudnessLearning(!safe->loudness_learning_);
                               } else if (r == 4) {
                                   safe->loudness_learning_ = false;
                                   b.getController().setLoudnessMatchON(false);
                                   safe->output_.setLabel("Output");
                                   safe->output_.setAccent(colour::kTextTertiary);
                               } else if (r >= 200) {
                                   b.setNow({{b.globalParam(zlp::PLookahead::kID), kLookaheads[static_cast<size_t>(r - 200)]}},
                                            "Lookahead");
                               } else if (r >= 100) {
                                   b.setNow({{b.globalParam(zlp::POutputStereo::kID), static_cast<float>(r - 100)}},
                                            "Output Channel");
                               }
                           });
    }

    void Footer::tick() {
        const auto& g = bridge_.global();
        phase_.setText(kPhaseNames[std::min<size_t>(static_cast<size_t>(g.structure), 5)]);
        pre_.setOn(g.fft_pre);
        post_.setOn(g.fft_post);
        side_spectrum_.setOn(g.fft_side);
        ext_side_.setOn(g.ext_side);
        auto_gain_.setOn(g.auto_gain);
        scale_.setValue(g.gain_scale);
        output_.setValue(g.output_gain);
        bypass_.setOn(g.bypass);
        match_.setOn(is_matching && is_matching());
    }

    void Footer::showAnalyzerMenu() {
        auto& na = bridge_.getProcessor().parameters_NA_;
        auto choice_menu = [&na](const juce::String& id, const juce::StringArray& names, const int base) {
            juce::PopupMenu m;
            const auto current = juce::roundToInt(na.getRawParameterValue(id)->load());
            for (int i = 0; i < names.size(); ++i) {
                m.addItem(base + i, names[i], true, i == current);
            }
            return m;
        };
        juce::PopupMenu menu;
        menu.addSectionHeader("Analyzer");
        menu.addSubMenu("Speed", choice_menu(zlstate::PFFTSpeed::kID, zlstate::PFFTSpeed::kChoices, 100));
        menu.addSubMenu("Range", choice_menu(zlstate::PFFTMinDB::kID, zlstate::PFFTMinDB::kChoices, 200));
        menu.addSubMenu("Ceiling", choice_menu(zlstate::PFFTTopDB::kID, zlstate::PFFTTopDB::kChoices, 300));
        menu.addSubMenu("Tilt", choice_menu(zlstate::PFFTTilt::kID, zlstate::PFFTTilt::kChoices, 400));
        menu.addSubMenu("Channel", choice_menu(zlstate::PFFTStereo::kID, zlstate::PFFTStereo::kChoices, 500));
        menu.addSeparator();
        const auto collision_on = na.getRawParameterValue(zlstate::PCollisionON::kID)->load() > .5f;
        const auto strength = na.getRawParameterValue(zlstate::PCollisionStrength::kID)->load();
        juce::PopupMenu strengths;
        for (int i = 1; i <= 4; ++i) {
            strengths.addItem(700 + i, juce::String(i * 25) + "%", true, std::abs(strength - static_cast<float>(i) * .25f) < .01f);
        }
        menu.addItem(601, "Collision Detection", true, collision_on);
        menu.addSubMenu("Collision Sensitivity", strengths, collision_on);
        menu.addItem(602, "Piano Roll", true, na.getRawParameterValue(zlstate::PPianoRollON::kID)->load() > .5f);
        menu.addItem(600, "Freeze on Hover", true, bridge_.global().fft_frozen);
        showPopupMenu(menu, *this, juce::PopupMenu::Options().withTargetComponent(&analyzer_menu_),
                           [safe = juce::Component::SafePointer(this)](const int r) {
                               if (safe == nullptr || r == 0) {
                                   return;
                               }
                               auto& b = safe->bridge_;
                               auto& na_tree = b.getProcessor().parameters_NA_;
                               auto toggle = [&](const char* id) {
                                   b.setNA(id, na_tree.getRawParameterValue(id)->load() > .5f ? 0.f : 1.f);
                               };
                               if (r == 600) {
                                   b.setNA(zlstate::PFFTFreezeON::kID, b.global().fft_frozen ? 0.f : 1.f);
                               } else if (r == 601) {
                                   toggle(zlstate::PCollisionON::kID);
                               } else if (r == 602) {
                                   toggle(zlstate::PPianoRollON::kID);
                               } else if (r > 700) {
                                   b.setNA(zlstate::PCollisionStrength::kID, static_cast<float>(r - 700) * .25f);
                               } else if (r >= 500) {
                                   b.setNA(zlstate::PFFTStereo::kID, static_cast<float>(r - 500));
                               } else if (r >= 400) {
                                   b.setNA(zlstate::PFFTTilt::kID, static_cast<float>(r - 400));
                               } else if (r >= 300) {
                                   b.setNA(zlstate::PFFTTopDB::kID, static_cast<float>(r - 300));
                               } else if (r >= 200) {
                                   b.setNA(zlstate::PFFTMinDB::kID, static_cast<float>(r - 200));
                               } else if (r >= 100) {
                                   b.setNA(zlstate::PFFTSpeed::kID, static_cast<float>(r - 100));
                               }
                           });
    }

    void Footer::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, 6.f);
        phase_.setBounds(b.removeFromLeft(118.f).toNearestInt());
        b.removeFromLeft(size::kS4);
        for (auto* chip : {&pre_, &post_, &side_spectrum_}) {
            chip->setBounds(b.removeFromLeft(static_cast<float>(chip->getIdealWidth())).toNearestInt());
            b.removeFromLeft(2.f);
        }
        analyzer_menu_.setBounds(b.removeFromLeft(22.f).toNearestInt());
        b.removeFromLeft(size::kS4);
        match_.setBounds(b.removeFromLeft(static_cast<float>(match_.getIdealWidth())).toNearestInt());

        b.removeFromRight(10.f); // window resize grip
        bypass_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(size::kS2);
        output_menu_.setBounds(b.removeFromRight(20.f).toNearestInt());
        output_.setBounds(b.removeFromRight(118.f).toNearestInt());
        b.removeFromRight(4.f);
        scale_.setBounds(b.removeFromRight(92.f).toNearestInt());
        b.removeFromRight(size::kS2);
        auto_gain_.setBounds(b.removeFromRight(static_cast<float>(auto_gain_.getIdealWidth())).toNearestInt());
        b.removeFromRight(2.f);
        ext_side_.setBounds(b.removeFromRight(static_cast<float>(ext_side_.getIdealWidth())).toNearestInt());
    }

    void Footer::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        hairline(g, 0.f, b.getWidth());
        // quiet separators between groups
        g.setColour(colour::kHairline);
        const auto x1 = static_cast<float>(phase_.getRight()) + size::kS4 * .5f;
        g.fillRect(juce::Rectangle<float>(x1, 10.f, 1.f, b.getHeight() - 20.f));
        const auto x3 = static_cast<float>(match_.getX()) - size::kS4 * .5f;
        g.fillRect(juce::Rectangle<float>(x3, 10.f, 1.f, b.getHeight() - 20.f));
        const auto x2 = static_cast<float>(ext_side_.getX()) - size::kS2 * .5f - 2.f;
        g.fillRect(juce::Rectangle<float>(x2, 10.f, 1.f, b.getHeight() - 20.f));
    }

    // ---------------------------------------------------------------- OutputRail

    OutputRail::OutputRail(EngineBridge& bridge, SpectrumWorker& spectrum) : bridge_(bridge), spectrum_(spectrum) {
        setTitle("Output meter");
    }

    void OutputRail::resized() {
        auto b = getLocalBounds().toFloat().reduced(6.f, 8.f);
        range_area_ = b.removeFromTop(22.f);
        b.removeFromTop(8.f);
        peak_area_ = b.removeFromBottom(16.f);
        b.removeFromBottom(6.f);
        meter_area_ = b.withSizeKeepingCentre(14.f, b.getHeight());
    }

    void OutputRail::tick(const double dt) {
        const auto left = spectrum_.takeOutputPeak(0);
        const auto right = spectrum_.takeOutputPeak(1);
        if (meter_.tick(dt, left, right) || bridge_.haveGlobalsChanged()) {
            repaint();
        }
    }

    void OutputRail::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, 0.f, 1.f, b.getHeight()));

        // EQ range selector
        g.setColour(colour::kControl);
        g.fillRoundedRectangle(range_area_, size::kRadiusSmall);
        g.setColour(colour::kTextSecondary);
        g.setFont(Fonts::get().font(type::kCaption, Fonts::kSemiBold));
        const auto max_db = zlstate::PEQMaxDB::kDBs[bridge_.global().eq_max_db_idx];
        g.drawText(juce::String(static_cast<int>(max_db)) + " dB", range_area_, juce::Justification::centred);

        meter_.paint(g, meter_area_, peak_area_);
    }

    void OutputRail::mouseUp(const juce::MouseEvent& e) {
        if (range_area_.contains(e.position)) {
            juce::PopupMenu menu;
            menu.addSectionHeader("Display Range");
            for (int i = 0; i < static_cast<int>(zlstate::PEQMaxDB::kDBs.size()); ++i) {
                menu.addItem(i + 1, juce::String(juce::CharPointer_UTF8("\xc2\xb1"))
                             + juce::String(static_cast<int>(zlstate::PEQMaxDB::kDBs[static_cast<size_t>(i)])) + " dB",
                             true, static_cast<size_t>(i) == bridge_.global().eq_max_db_idx);
            }
            showPopupMenu(menu, *this, juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(range_area_.toNearestInt())),
                               [safe = juce::Component::SafePointer(this)](const int r) {
                                   if (safe != nullptr && r > 0) {
                                       safe->bridge_.setNA(zlstate::PEQMaxDB::kID, static_cast<float>(r - 1));
                                   }
                               });
        } else if (meter_area_.expanded(8.f, 0.f).contains(e.position) || peak_area_.contains(e.position)) {
            meter_.resetPeak();
            repaint();
        }
    }
}
