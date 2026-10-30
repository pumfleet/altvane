// Copyright (C) 2026 - Altvane
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "editor.hpp"

#include "../PluginProcessor.hpp"
#include "factory_presets.hpp"
#include "theme/compressor_tokens.hpp"

namespace altvane::compressor {
    using namespace ui::tokens;

    namespace {
        // the knee display needs this much room above the control card
        constexpr int kKneeMin = 180, kKneeMax = 420, kKneeReserve = 196;

        ui::ShellConfig makeConfig(PluginProcessor& p) {
            ui::ShellConfig config;
            config.product = "COMPRESSOR";
            config.window = {
                size::kDefaultWidth, size::kDefaultHeight, size::kMinWidth, size::kMinHeight,
                size::kMaxWidth, size::kMaxHeight
            };
            // presets stay in the upstream ZL Compressor JSON format, so they load in either plug-in
            config.preset_codec = presetCodec(p);
            config.credit = juce::String(juce::CharPointer_UTF8(
                "Based on ZL Compressor by ZL Audio  \xc2\xb7  AGPL-3.0"));
            config.switches.push_back({"HELP", "Help balloons", Editor::kHelpSwitch, true});
            return config;
        }
    }

    Editor::Editor(PluginProcessor& p) :
        EditorShell(p, p.parameters_, p.session_, makeConfig(p)),
        p_ref_(p),
        bridge_(p, param_editor_),
        history_(p),
        spectrum_(p),
        level_(bridge_, history_),
        knee_(bridge_, model_),
        card_(bridge_),
        advanced_(bridge_),
        rail_(bridge_),
        side_panel_(bridge_, spectrum_),
        footer_(bridge_),
        help_(root_) {
        presets_.installFactoryPresets(factoryPresets(p), kFactoryPresetsVersion);
        model_.sync(bridge_);

        root_.addAndMakeVisible(level_);
        root_.addAndMakeVisible(knee_);
        root_.addAndMakeVisible(knee_tab_);
        root_.addAndMakeVisible(card_);
        root_.addAndMakeVisible(side_button_);
        root_.addAndMakeVisible(rail_);
        root_.addChildComponent(side_panel_);
        root_.addAndMakeVisible(footer_);
        root_.addChildComponent(advanced_);
        // last, so it draws over everything
        root_.addAndMakeVisible(help_);
        help_.setActive(getUISettings().getSwitch(kHelpSwitch));

        knee_tab_.on_click = [this] {
            bridge_.setDisplay(zlstate::PComputerCurveDisplay::kID, isKneeOpen() ? 0.f : 1.f);
            layoutDisplay();
        };
        side_button_.on_click = [this] {
            bridge_.setDisplay(zlstate::PSideControlDisplay::kID, isSideChainOpen() ? 0.f : 1.f);
        };
        card_.on_more = [this] { setAdvancedOpen(!advanced_.isVisible()); };
        advanced_.on_close = [this] { setAdvancedOpen(false); };
        // clicks anywhere else close the advanced controls
        root_.addMouseListener(this, true);

        side_fade_.snap(isSideChainOpen());
        finishSetup();
    }

    Editor::~Editor() {
        stopFrames();
        root_.removeMouseListener(this);
        spectrum_.setOn(false);
    }

    bool Editor::isKneeOpen() const {
        return bridge_.getDisplayChoice(zlstate::PComputerCurveDisplay::kID) == 1;
    }

    bool Editor::isSideChainOpen() const {
        return bridge_.getDisplayChoice(zlstate::PSideControlDisplay::kID) == 1;
    }

    void Editor::setAdvancedOpen(const bool open) {
        if (open == advanced_.isVisible()) {
            return;
        }
        if (open) {
            const auto more = root_.getLocalArea(&card_, card_.getMoreButton().getBounds());
            const auto size = AdvancedPopover::getIdealSize();
            const auto x = std::clamp(more.getX() - 20, area_.getX() + 8, area_.getRight() - size.x - 8);
            advanced_.setBounds(x, card_.getY() - size.y - 10, size.x, size.y);
            advanced_.tick();
            advanced_.setVisible(true);
            advanced_.toFront(false);
        } else {
            advanced_.setVisible(false);
        }
        card_.setMoreOpen(open);
    }

    void Editor::mouseDown(const juce::MouseEvent& e) {
        if (!advanced_.isVisible()) {
            return;
        }
        auto* c = e.eventComponent;
        const auto inside = c == &advanced_ || advanced_.isParentOf(c) || c == &card_.getMoreButton();
        if (!inside) {
            setAdvancedOpen(false);
        }
    }

    bool Editor::keyPressed(const juce::KeyPress& key) {
        if (key == juce::KeyPress::escapeKey && advanced_.isVisible()) {
            setAdvancedOpen(false);
            return true;
        }
        return EditorShell::keyPressed(key);
    }

    void Editor::layout(const juce::Rectangle<int> area) {
        area_ = area;
        layoutDisplay();
    }

    void Editor::layoutDisplay() {
        auto b = area_;
        footer_.setBounds(b.removeFromBottom(static_cast<int>(size::kFooterHeight)));
        // the side-chain drawer pushes the display up as it opens
        const auto drawer = juce::roundToInt(side_fade_.getEased() * size::kSideChainHeight);
        if (drawer > 0) {
            side_panel_.setBounds(b.removeFromBottom(drawer).withHeight(static_cast<int>(size::kSideChainHeight)));
            side_panel_.setVisible(true);
        } else {
            side_panel_.setVisible(false);
        }
        rail_.setBounds(b.removeFromRight(static_cast<int>(size::kMeterRailWidth)));
        level_.setBounds(b);

        // the knee display is square, top right, when there is room for it above the card
        const auto knee = std::min(b.getHeight() - kKneeReserve, kKneeMax);
        knee_fits_ = knee >= kKneeMin;
        const auto show_knee = knee_fits_ && isKneeOpen();
        knee_.setVisible(show_knee);
        knee_tab_.setVisible(knee_fits_);
        auto plot_right = static_cast<float>(b.getWidth()) - 40.f;
        if (show_knee) {
            knee_.setBounds(b.getRight() - 12 - knee, b.getY() + 12, knee, knee);
            knee_tab_.setBounds(knee_.getX() - 18, b.getY() + 24, 18, 54);
            plot_right = static_cast<float>(knee_.getX() - b.getX()) - 12.f - 40.f;
        } else {
            knee_tab_.setBounds(b.getRight() - 18, b.getY() + 24, 18, 54);
            plot_right = static_cast<float>(b.getWidth()) - 18.f - 40.f;
        }
        level_.setPlotRight(plot_right);
        knee_tab_.setOpen(show_knee);

        const auto card = ControlCard::getIdealSize();
        const auto card_w = std::min(card.x, b.getWidth() - 16);
        card_.setBounds(juce::Rectangle<int>(card_w, card.y)
            .withCentre({b.getCentreX(), 0})
            .withBottomY(b.getBottom() - static_cast<int>(size::kCardBottom)));
        const auto pill_w = side_button_.getIdealWidth();
        side_button_.setBounds(juce::Rectangle<int>(pill_w, 24).withCentre({b.getCentreX(), 0})
                                                              .withBottomY(b.getBottom() - 8));
        if (advanced_.isVisible()) {
            advanced_.setVisible(false);
            setAdvancedOpen(true);
        }
    }

    void Editor::settingsApplied() {
        help_.setActive(getUISettings().getSwitch(kHelpSwitch));
    }

    void Editor::showingChanged(const bool showing) {
        p_ref_.getCompressController().setMagAnalyzerOn(showing);
        spectrum_.setOn(showing && isSideChainOpen());
        if (!showing) {
            history_.clear();
        }
    }

    void Editor::tick(const double dt) {
        // the side-chain drawer
        const auto side_open = isSideChainOpen();
        side_fade_.setTarget(side_open);
        if (side_fade_.update(static_cast<float>(dt))) {
            layoutDisplay();
        }
        side_button_.setOpen(side_open);
        side_button_.setActive(bridge_.isSideChainActive());
        spectrum_.setOn(side_open);

        // the knee display opens and closes from its tab, or by its saved state
        if (knee_fits_ && isKneeOpen() != knee_.isVisible()) {
            layoutDisplay();
        }

        history_.setGeometry(level_.getPlotWidth(), zlstate::PAnalyzerTimeLength::kLength[static_cast<size_t>(
                                 std::clamp(bridge_.getDisplayChoice(zlstate::PAnalyzerTimeLength::kID), 0, 3))]);
        history_.update();
        const auto meters = history_.takeMeters();
        const auto side = meters.fresh ? std::optional(meters.side) : std::nullopt;

        // bypassed, the displays step back
        const auto alpha = bridge_.getBool(zlp::PCompON::kID) ? 1.f : .55f;
        if (std::abs(level_.getAlpha() - alpha) > 1e-3f) {
            level_.setAlpha(alpha);
            knee_.setAlpha(alpha);
        }
        level_.tick();
        if (knee_.isVisible()) {
            knee_.tick(dt, side);
        }
        rail_.tick(dt, meters);
        card_.tick(dt, side);
        footer_.tick();
        if (advanced_.isVisible()) {
            advanced_.tick();
        }
        if (side_panel_.isVisible()) {
            spectrum_.update(static_cast<float>(getFrameRate()));
            side_panel_.tick();
        }
    }
}
