// Copyright (C) 2026 - Altvane
// This file is part of Altvane Limiter
//
// Altvane Limiter is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Limiter is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Limiter. If not, see <https://www.gnu.org/licenses/>.

#include "editor.hpp"

#include "factory_presets.hpp"

namespace altvane::limiter {
    using namespace tokens;
    using ui::Fonts;

    namespace {
        ui::ShellConfig makeConfig(PluginProcessor& p) {
            ui::ShellConfig config;
            config.product = "LIMITER";
            config.window = {
                size::kDefaultWidth, size::kDefaultHeight, size::kMinWidth, size::kMinHeight,
                size::kMaxWidth, size::kMaxHeight
            };
            config.preset_codec = presetCodec(p);
            config.credit = juce::String::fromUTF8("Audio engine: ZL Limiter by zsliu98  \xc2\xb7  AGPL-3.0");
            config.switches.push_back({"HELP", "Help balloons", Editor::kHelpSwitch, true});
            return config;
        }

        void setSession(PluginProcessor& p, const char* id, const bool on) {
            auto* para = p.na_parameters_.getParameter(id);
            ui::ParamEditor::setNotifyingHost(para, on ? 1.f : 0.f);
        }
    }

    // ---------------------------------------------------------------- PanelTab

    PanelTab::PanelTab(const juce::String& text) : text_(text) {
        setTitle(text.toLowerCase().substring(0, 1).toUpperCase() + text.toLowerCase().substring(1) + " panel");
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void PanelTab::setOpen(const bool open) {
        if (open != open_) {
            open_ = open;
            repaint();
        }
    }

    void PanelTab::paint(juce::Graphics& g) {
        const auto b = getLocalBounds().toFloat();
        juce::Path shape;
        shape.addRoundedRectangle(b.getX(), b.getY(), b.getWidth() + 6.f, b.getHeight(), size::kRadius, size::kRadius,
                                  true, false, true, false);
        g.setColour(hovered_ ? colour::kControl : colour::kRaised);
        g.fillPath(shape);
        g.setColour(colour::kHairlineStrong);
        g.strokePath(shape, juce::PathStrokeType(1.f));
        g.setColour(open_ ? colour::kTextSecondary : hovered_ ? colour::kTextPrimary : colour::kTextSecondary);
        g.setFont(Fonts::get().font(9.f, Fonts::kSemiBold).withExtraKerningFactor(.14f));
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, b.getCentreX(),
                                                       b.getCentreY()));
        g.drawText(text_, juce::Rectangle<float>(b.getHeight(), b.getWidth()).withCentre(b.getCentre()),
                   juce::Justification::centred);
    }

    void PanelTab::mouseUp(const juce::MouseEvent& e) {
        if (e.mouseWasClicked() && on_click) {
            on_click();
        }
    }

    bool PanelTab::keyPressed(const juce::KeyPress& key) {
        if ((key == juce::KeyPress::spaceKey || key == juce::KeyPress::returnKey) && on_click) {
            on_click();
            return true;
        }
        return false;
    }

    std::unique_ptr<juce::AccessibilityHandler> PanelTab::createAccessibilityHandler() {
        return std::make_unique<juce::AccessibilityHandler>(
            *this, juce::AccessibilityRole::button,
            juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] {
                if (on_click) {
                    on_click();
                }
            }));
    }

    // ---------------------------------------------------------------- Editor

    Editor::Editor(PluginProcessor& p) :
        EditorShell(p, p.parameters_, p.session_, makeConfig(p)),
        p_ref_(p),
        feed_(p),
        history_(p, param_editor_, feed_),
        rail_(p),
        card_(p, param_editor_),
        loudness_(p, param_editor_),
        footer_(p, param_editor_),
        help_(root_) {
        presets_.installFactoryPresets(factoryPresets(p), kFactoryPresetsVersion);
        for (auto* c : std::initializer_list<juce::Component*>{
                 &history_, &rail_, &footer_, &card_, &loudness_, &loudness_tab_, &show_controls_, &help_}) {
            root_.addAndMakeVisible(c);
        }
        loudness_open_ = value(p.na_parameters_, zlstate::PLoudnessPanelOpen::kID) > .5f;
        controls_open_ = value(p.na_parameters_, zlstate::PControlsOpen::kID) > .5f;
        loudness_tab_.on_click = [this] { setLoudnessOpen(!loudness_open_); };
        ui::setHelp(loudness_tab_, "Loudness", "Show or hide the loudness panel.");
        card_.on_collapse = [this] { setControlsOpen(false); };
        show_controls_.on_click = [this] { setControlsOpen(true); };
        help_.setActive(getUISettings().getSwitch(kHelpSwitch));
        settings_sheet_.setCardSize({560, 500});
        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
        feed_.setActive(false);
    }

    void Editor::setLoudnessOpen(const bool open) {
        loudness_open_ = open;
        setSession(p_ref_, zlstate::PLoudnessPanelOpen::kID, open);
        placeOverlays();
    }

    void Editor::setControlsOpen(const bool open) {
        controls_open_ = open;
        setSession(p_ref_, zlstate::PControlsOpen::kID, open);
        placeOverlays();
    }

    void Editor::layout(const juce::Rectangle<int> area) {
        auto b = area;
        footer_.setBounds(b.removeFromBottom(static_cast<int>(size::kFooterHeight)));
        rail_.setBounds(b.removeFromRight(static_cast<int>(size::kRailWidth)));
        history_.setBounds(b);
        display_ = b;
        help_.setBounds(help_.getBounds());
        placeOverlays();
    }

    void Editor::placeOverlays() {
        const auto d = display_.toFloat();
        constexpr float kMargin = 12.f;

        card_.setVisible(controls_open_);
        show_controls_.setVisible(!controls_open_);
        juce::Rectangle<float> card;
        if (controls_open_) {
            const auto w = std::min(ControlCard::getPreferredWidth(), d.getWidth() - 2.f * kMargin);
            card = juce::Rectangle<float>(w, size::kCardHeight)
                   .withCentre({d.getCentreX(), d.getBottom() - kMargin - size::kCardHeight * .5f});
            card_.setBounds(card.toNearestInt());
        } else {
            const auto w = static_cast<float>(show_controls_.getIdealWidth()) + 24.f;
            show_controls_.setBounds(juce::Rectangle<float>(w, 26.f)
                                     .withCentre({d.getCentreX(), d.getBottom() - kMargin - 13.f}).toNearestInt());
        }

        loudness_.setVisible(loudness_open_);
        const auto top = d.getY() + kMargin;
        const auto bottom = controls_open_ ? card.getY() - kMargin : d.getBottom() - kMargin;
        auto right_inset = 0.f;
        if (loudness_open_) {
            const auto preferred = LoudnessPanel::getPreferredHeight(custom_target_, true);
            const auto h = std::max(LoudnessPanel::getPreferredHeight(custom_target_, false),
                                    std::min(preferred, bottom - top));
            const auto panel = juce::Rectangle<float>(d.getRight() - kMargin - size::kLoudnessWidth, top,
                                                      size::kLoudnessWidth, h);
            loudness_.setBounds(panel.toNearestInt());
            right_inset = size::kLoudnessWidth + kMargin + size::kPanelTabWidth + 2.f;
            loudness_tab_.setBounds(juce::Rectangle<float>(panel.getX() - size::kPanelTabWidth + 1.f, top + 14.f,
                                                           size::kPanelTabWidth, size::kPanelTabHeight).toNearestInt());
        } else {
            loudness_tab_.setBounds(juce::Rectangle<float>(d.getRight() - size::kPanelTabWidth, top + 14.f,
                                                           size::kPanelTabWidth, size::kPanelTabHeight).toNearestInt());
            right_inset = size::kPanelTabWidth;
        }
        loudness_tab_.setOpen(loudness_open_);
        history_.setInsets(right_inset, controls_open_ ? size::kCardHeight + kMargin : 26.f + kMargin);
    }

    void Editor::tick(const double dt) {
        const auto& loudness = p_ref_.pullLoudness();
        const auto columns = feed_.pull(loudness.short_term, value(p_ref_.parameters_, zlp::PTruePeak::kID) > .5f);
        history_.tick(dt, columns, loudness);
        rail_.tick(dt, feed_, loudness);
        loudness_.tick(loudness, feed_.correlation);
        card_.tick();
        footer_.tick();

        // the panels follow the session (a preset, undo of the state, a host recall)
        const auto loudness_open = value(p_ref_.na_parameters_, zlstate::PLoudnessPanelOpen::kID) > .5f;
        const auto controls_open = value(p_ref_.na_parameters_, zlstate::PControlsOpen::kID) > .5f;
        const auto custom = index(p_ref_.na_parameters_, zlstate::PLoudnessTarget::kID) ==
                            static_cast<int>(kCustomTarget);
        if (loudness_open != loudness_open_ || controls_open != controls_open_ || custom != custom_target_) {
            loudness_open_ = loudness_open;
            controls_open_ = controls_open;
            custom_target_ = custom;
            placeOverlays();
        }
    }

    void Editor::showingChanged(const bool showing) {
        feed_.setActive(showing);
        if (!showing) {
            feed_.clear();
        }
    }

    void Editor::settingsApplied() {
        help_.setActive(getUISettings().getSwitch(kHelpSwitch));
    }

    int Editor::getControlParameterIndex(juce::Component& component) {
        if (&component == &history_) {
            return history_.getHoveredParameterIndex();
        }
        return EditorShell::getControlParameterIndex(component);
    }
}
