// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "chrome.hpp"

#include "../model/band_ops.hpp"
#include "../model/session_ops.hpp"
#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"
#include "../util/value_format.hpp"

namespace contour::ui {
    using namespace tokens;

    namespace {
        const std::array<const char*, 6> kPhaseNames{"Minimum", "SVF", "Parallel", "Matched", "Mixed", "Linear"};
        const std::array<const char*, 6> kPhaseDescriptions{
            "Minimum phase, zero latency", "State-variable filters, zero latency",
            "Parallel filters, zero latency", "Matched analog phase, adds latency",
            "Mixed phase, adds latency", "Linear phase, adds latency"
        };

        void hairline(juce::Graphics& g, const float y, const float w) {
            g.setColour(colour::kHairline);
            g.fillRect(juce::Rectangle<float>(0.f, y, w, 1.f));
        }
    }

    // ---------------------------------------------------------------- Header

    Header::Header(EngineBridge& bridge, PresetManager& presets) : bridge_(bridge), presets_(presets) {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &undo_, &redo_, &settings_, &prev_, &next_, &slot_a_, &slot_b_, &copy_slot_
             }) {
            addAndMakeVisible(c);
        }
        slot_a_.setTooltip("Compare: setting A");
        slot_b_.setTooltip("Compare: setting B");
        slot_a_.on_click = [this] {
            if (session_ops::getABSlot(bridge_) != 0) session_ops::switchAB(bridge_);
        };
        slot_b_.on_click = [this] {
            if (session_ops::getABSlot(bridge_) != 1) session_ops::switchAB(bridge_);
        };
        copy_slot_.on_click = [this] { session_ops::copyToOtherSlot(bridge_); };
        undo_.setTooltip("Undo (Cmd+Z)");
        redo_.setTooltip("Redo (Shift+Cmd+Z)");
        settings_.setTooltip("Settings");
        prev_.setTooltip("Previous preset");
        next_.setTooltip("Next preset");
        undo_.on_click = [this] { if (on_undo) on_undo(); };
        redo_.on_click = [this] { if (on_redo) on_redo(); };
        prev_.on_click = [this] { presets_.step(-1); };
        next_.on_click = [this] { presets_.step(1); };
        settings_.on_click = [this] {
            if (on_open_settings) {
                on_open_settings();
                return;
            }
            if (!build_settings_menu) {
                return;
            }
            build_settings_menu().showMenuAsync(
                juce::PopupMenu::Options().withTargetComponent(&settings_),
                [safe = juce::Component::SafePointer(this)](const int r) {
                    if (safe != nullptr && r != 0 && safe->on_settings_result) {
                        safe->on_settings_result(r);
                    }
                });
        };
    }

    void Header::tick() {
        auto& um = bridge_.getUndoManager();
        undo_.setAlpha(um.canUndo() ? 1.f : .35f);
        redo_.setAlpha(um.canRedo() ? 1.f : .35f);
        undo_.setTooltip(um.canUndo() ? "Undo " + um.getUndoDescription() : "Undo");
        redo_.setTooltip(um.canRedo() ? "Redo " + um.getRedoDescription() : "Redo");
        const auto slot = session_ops::getABSlot(bridge_);
        slot_a_.setOn(slot == 0);
        slot_b_.setOn(slot == 1);
        copy_slot_.setTooltip(slot == 0 ? "Copy A to B" : "Copy B to A");
        // comparing ~600 parameters is cheap, but there is no need to do it every frame
        if (--modified_check_countdown_ <= 0) {
            modified_check_countdown_ = 10;
            const auto name = presets_.getCurrentName();
            const auto modified = presets_.isModified();
            if (name != preset_name_ || modified != preset_modified_) {
                preset_name_ = name;
                preset_modified_ = modified;
                repaint();
            }
        }
    }

    void Header::resized() {
        auto b = getLocalBounds().toFloat().reduced(size::kS3, 7.f);
        settings_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(10.f);
        redo_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(2.f);
        undo_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(14.f);
        copy_slot_.setBounds(b.removeFromRight(26.f).toNearestInt());
        b.removeFromRight(2.f);
        slot_b_.setBounds(b.removeFromRight(24.f).toNearestInt());
        slot_a_.setBounds(b.removeFromRight(24.f).toNearestInt());

        preset_area_ = juce::Rectangle<float>(260.f, 26.f).withCentre(getLocalBounds().toFloat().getCentre());
        auto p = preset_area_;
        prev_.setBounds(p.removeFromLeft(26.f).toNearestInt());
        next_.setBounds(p.removeFromRight(26.f).toNearestInt());
    }

    void Header::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        g.setColour(colour::kWindow);
        g.fillRect(b);
        hairline(g, b.getBottom() - 1.f, b.getWidth());

        // mark: three nested contours
        auto mark = juce::Rectangle<float>(18.f, 18.f).withCentre({size::kS4 + 9.f, b.getCentreY()});
        for (int i = 0; i < 3; ++i) {
            juce::Path p;
            const auto inset = static_cast<float>(i) * 3.f;
            const auto r = mark.reduced(0.f, inset);
            p.startNewSubPath(r.getX(), r.getBottom());
            p.cubicTo(r.getX() + r.getWidth() * .35f, r.getBottom(), r.getX() + r.getWidth() * .35f, r.getY(),
                      r.getCentreX(), r.getY());
            p.cubicTo(r.getX() + r.getWidth() * .65f, r.getY(), r.getX() + r.getWidth() * .65f, r.getBottom(),
                      r.getRight(), r.getBottom());
            g.setColour(colour::kAccent.withAlpha(1.f - static_cast<float>(i) * .3f));
            g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(colour::kTextPrimary);
        g.setFont(Fonts::get().font(type::kTitle - 1.f, Fonts::kSemiBold).withExtraKerningFactor(.02f));
        g.drawText("Contour", juce::Rectangle<float>(mark.getRight() + 9.f, 0.f, 120.f, b.getHeight()),
                   juce::Justification::centredLeft);

        // preset pill
        g.setColour(preset_hovered_ ? colour::kControlHover : colour::kControl);
        g.fillRoundedRectangle(preset_area_, size::kRadiusSmall);
        g.setColour(colour::kTextPrimary);
        g.setFont(Fonts::get().font(type::kBody));
        const auto text_area = preset_area_.reduced(30.f, 0.f);
        g.drawText(preset_name_, text_area, juce::Justification::centred, true);
        if (preset_modified_) {
            const auto w = juce::GlyphArrangement::getStringWidth(Fonts::get().font(type::kBody), preset_name_);
            g.setColour(colour::kTextTertiary);
            g.fillEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre({
                std::min(text_area.getCentreX() + w * .5f + 8.f, text_area.getRight()), text_area.getCentreY()
            }));
        }
    }

    void Header::mouseMove(const juce::MouseEvent& e) {
        const auto h = preset_area_.reduced(26.f, 0.f).contains(e.position);
        if (h != preset_hovered_) {
            preset_hovered_ = h;
            repaint();
        }
    }

    void Header::mouseExit(const juce::MouseEvent&) {
        preset_hovered_ = false;
        repaint();
    }

    void Header::mouseUp(const juce::MouseEvent& e) {
        if (preset_area_.reduced(26.f, 0.f).contains(e.position)) {
            if (e.mods.isPopupMenu()) {
                showPresetMenu();
            } else if (on_open_presets) {
                on_open_presets();
            }
        }
    }

    void Header::showPresetMenu() {
        juce::PopupMenu menu;
        menu.addItem(1, "Browse Presets...");
        menu.addItem(2, "Save", presets_.getCurrentFile() != juce::File());
        menu.addItem(3, "Save As...");
        menu.addSeparator();
        menu.addItem(4, "Initialise");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(preset_area_.toNearestInt()))
                                                     .withMinimumWidth(juce::roundToInt(preset_area_.getWidth())),
                           [safe = juce::Component::SafePointer(this)](const int r) {
                               if (safe == nullptr) {
                                   return;
                               }
                               if (r == 1 && safe->on_open_presets) {
                                   safe->on_open_presets();
                               } else if (r == 2) {
                                   safe->presets_.save(safe->presets_.getCurrentFile());
                               } else if (r == 3) {
                                   safe->savePresetAs();
                               } else if (r == 4) {
                                   safe->presets_.initialise();
                               }
                           });
    }

    void Header::savePresetAs() {
        const auto dir = PresetManager::getDirectory();
        dir.createDirectory();
        chooser_ = std::make_unique<juce::FileChooser>("Save Preset", dir.getChildFile(presets_.getCurrentName() + ".json"),
                                                       "*.json");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer(this)](const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult() != juce::File()) {
                                      safe->presets_.save(fc.getResult().withFileExtension("json"));
                                  }
                              });
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
                menu.addItem(i + 1, juce::String(kPhaseNames[static_cast<size_t>(i)]) + "   "
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

        bindField(scale_, zlp::PGainScale::kID, "Scale");
        scale_.format = [](const float v) { return format::percent(v); };
        bindField(output_, zlp::POutputGain::kID, "Output");
        output_.format = [](const float v) { return format::gain(v); };

        output_menu_.setTooltip("Output options");
        output_menu_.on_click = [this] { showOutputMenu(); };

        bypass_.setToggleable(true);
        scale_.setComponentID(zlp::PGainScale::kID);
        output_.setComponentID(zlp::POutputGain::kID);
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
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&output_menu_),
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

    void Footer::bindField(ValueField& field, const char* id, const juce::String& name) {
        auto* para = bridge_.globalParam(id);
        field.setLabel(name);
        field.setRange(para->getNormalisableRange(), para->convertFrom0to1(para->getDefaultValue()));
        field.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = format::parseNumber(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        field.on_begin = [this, para, name] {
            juce::RangedAudioParameter* paras[] = {para};
            bridge_.beginEdit(paras, "Change " + name);
        };
        field.on_change = [this, para](const float v) { bridge_.setPlain(para, v); };
        field.on_end = [this] { bridge_.endEdit(); };
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
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&analyzer_menu_),
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
        bool changed = false;
        for (size_t c = 0; c < 2; ++c) {
            const auto peak = spectrum_.takeOutputPeak(c);
            const auto decayed = levels_[c] - static_cast<float>(dt) * 24.f;
            const auto next = std::max(peak, decayed);
            changed = changed || std::abs(next - levels_[c]) > .05f;
            levels_[c] = std::max(next, -120.f);
            if (peak >= peak_hold_) {
                peak_hold_ = peak;
                peak_age_ = 0.0;
            }
        }
        peak_age_ += dt;
        if (peak_age_ > 2.0) {
            peak_hold_ = std::max(levels_[0], levels_[1]);
            peak_age_ = 1.5;
            changed = true;
        }
        if (changed || bridge_.haveGlobalsChanged()) {
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

        // meter scale: +6 .. -60 dB
        auto to_y = [this](const float db) {
            const auto t = std::clamp((6.f - db) / 66.f, 0.f, 1.f);
            return meter_area_.getY() + t * meter_area_.getHeight();
        };
        const auto bar_w = (meter_area_.getWidth() - 2.f) * .5f;
        for (size_t c = 0; c < 2; ++c) {
            auto bar = juce::Rectangle<float>(meter_area_.getX() + static_cast<float>(c) * (bar_w + 2.f),
                                              meter_area_.getY(), bar_w, meter_area_.getHeight());
            g.setColour(colour::kControl);
            g.fillRoundedRectangle(bar, 1.5f);
            const auto top = to_y(levels_[c]);
            if (top < bar.getBottom()) {
                auto fill = bar.withTop(top);
                g.setGradientFill(juce::ColourGradient(colour::kDanger, 0.f, to_y(3.f),
                                                       colour::kAccent.withAlpha(.75f), 0.f, to_y(-24.f), false));
                g.fillRoundedRectangle(fill, 1.5f);
            }
        }
        // 0 dB reference across both bars
        g.setColour(colour::kGraph);
        g.fillRect(juce::Rectangle<float>(meter_area_.getX(), to_y(0.f), meter_area_.getWidth(), 1.f));

        g.setColour(peak_hold_ > 0.f ? colour::kDanger : colour::kTextSecondary);
        Fonts::get().drawTabular(g, peak_hold_ < -99.f ? juce::String(juce::CharPointer_UTF8("\xe2\x80\x93"))
                                                       : juce::String(peak_hold_, 1),
                                 peak_area_, juce::Justification::centred, type::kCaption);
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
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(range_area_.toNearestInt())),
                               [safe = juce::Component::SafePointer(this)](const int r) {
                                   if (safe != nullptr && r > 0) {
                                       safe->bridge_.setNA(zlstate::PEQMaxDB::kID, static_cast<float>(r - 1));
                                   }
                               });
        } else if (meter_area_.expanded(8.f, 0.f).contains(e.position) || peak_area_.contains(e.position)) {
            peak_hold_ = -120.f;
            repaint();
        }
    }
}
