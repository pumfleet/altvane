// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "header.hpp"

#include "../theme/brand.hpp"
#include "../theme/fonts.hpp"
#include "../theme/look_and_feel.hpp"
#include "../theme/tokens.hpp"

namespace altvane::ui {
    using namespace tokens;

    Header::Header(const juce::String& product, ParamEditor& editor, Session& session,
                   juce::AudioProcessorValueTreeState& parameters, PresetManager& presets) :
        product_(product), editor_(editor), session_(session), parameters_(parameters), presets_(presets) {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &undo_, &redo_, &settings_, &prev_, &next_, &slot_a_, &slot_b_, &copy_slot_
             }) {
            addAndMakeVisible(c);
        }
        slot_a_.setTooltip("Compare: setting A");
        slot_b_.setTooltip("Compare: setting B");
        slot_a_.on_click = [this] { selectSlot(0); };
        slot_b_.on_click = [this] { selectSlot(1); };
        copy_slot_.on_click = [this] { session_.copyToOtherSlot(parameters_); };
        undo_.setTooltip("Undo (Cmd+Z)");
        redo_.setTooltip("Redo (Shift+Cmd+Z)");
        settings_.setTooltip("Settings");
        prev_.setTooltip("Previous preset");
        next_.setTooltip("Next preset");
        undo_.on_click = [this] { if (on_undo) on_undo(); };
        redo_.on_click = [this] { if (on_redo) on_redo(); };
        prev_.on_click = [this] { presets_.step(-1); };
        next_.on_click = [this] { presets_.step(1); };
        settings_.on_click = [this] { if (on_open_settings) on_open_settings(); };
    }

    void Header::selectSlot(const int slot) {
        if (session_.getABSlot() == slot) {
            return;
        }
        if (editor_.isEditing()) {
            editor_.endEdit();
        }
        session_.switchAB(parameters_);
    }

    void Header::tick() {
        auto& um = editor_.getUndoManager();
        undo_.setAlpha(um.canUndo() ? 1.f : .35f);
        redo_.setAlpha(um.canRedo() ? 1.f : .35f);
        undo_.setTooltip(um.canUndo() ? "Undo " + um.getUndoDescription() : "Undo");
        redo_.setTooltip(um.canRedo() ? "Redo " + um.getRedoDescription() : "Redo");
        const auto slot = session_.getABSlot();
        slot_a_.setOn(slot == 0);
        slot_b_.setOn(slot == 1);
        copy_slot_.setTooltip(slot == 0 ? "Copy A to B" : "Copy B to A");
        // comparing every parameter is cheap, but there is no need to do it every frame
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
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(0.f, b.getBottom() - 1.f, b.getWidth(), 1.f));

        brand::drawLockup(g, b.withTrimmedLeft(size::kS4), product_);

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
        showPopupMenu(menu, *this, juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(preset_area_.toNearestInt()))
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
}
