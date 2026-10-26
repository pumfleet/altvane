// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "preset_browser.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"

namespace contour::ui {
    using namespace tokens;

    namespace {
        void styleEditor(juce::TextEditor& editor, const juce::String& placeholder) {
            editor.setFont(Fonts::get().font(type::kBody));
            editor.setTextToShowWhenEmpty(placeholder, colour::kTextTertiary);
            editor.setIndents(10, 0);
            editor.setJustification(juce::Justification::centredLeft);
            editor.setBorder({});
        }

        void styleList(juce::ListBox& list) {
            list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            list.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
            list.setRowHeight(28);
            list.setOutlineThickness(0);
        }
    }

    PresetBrowser::PresetBrowser(PresetManager& presets) : Sheet("Presets"), presets_(presets) {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &search_, &groups_, &list_, &new_group_, &reveal_, &import_, &delete_, &rename_, &save_as_, &load_
             }) {
            addAndMakeVisible(c);
        }
        addChildComponent(entry_);
        styleEditor(search_, "Search presets");
        styleEditor(entry_, "");
        styleList(groups_);
        styleList(list_);
        search_.onTextChange = [this] { applyFilter(); };
        search_.onReturnKey = [this] { loadSelected(); };
        search_.onEscapeKey = [this] {
            if (search_.isEmpty()) {
                close();
            } else {
                search_.clear();
                applyFilter();
            }
        };
        search_.addKeyListener(this);
        entry_.onReturnKey = [this] { commitEntry(); };
        entry_.onEscapeKey = [this] { cancelEntry(); };

        new_group_.setTooltip("New folder");
        new_group_.on_click = [this] { beginEntry(Entry::kNewGroup); };
        reveal_.setTooltip("Show in Finder");
        reveal_.on_click = [] {
            PresetManager::getDirectory().createDirectory();
            PresetManager::getDirectory().revealToUser();
        };
        import_.on_click = [this] {
            chooser_ = std::make_unique<juce::FileChooser>("Import Preset", juce::File(), "*.json");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe = juce::Component::SafePointer(this)](const juce::FileChooser& fc) {
                                      const auto file = fc.getResult();
                                      if (safe == nullptr || !file.existsAsFile()) {
                                          return;
                                      }
                                      const auto target = safe->getTargetGroupDir().getNonexistentChildFile(
                                          file.getFileNameWithoutExtension(), PresetManager::kExtension, false);
                                      target.getParentDirectory().createDirectory();
                                      if (file.copyFileTo(target) && safe->presets_.load(target)) {
                                          safe->refresh();
                                      }
                                  });
        };
        delete_.on_click = [this] {
            const auto* selected = getSelected();
            if (selected == nullptr) {
                return;
            }
            if (!confirm_delete_) {
                confirm_delete_ = true;
                updateButtons();
                startTimer(3000);
                return;
            }
            confirm_delete_ = false;
            stopTimer();
            presets_.remove(selected->file);
            refresh();
        };
        rename_.on_click = [this] { beginEntry(Entry::kRename); };
        save_as_.on_click = [this] { beginEntry(Entry::kSaveAs); };
        load_.on_click = [this] { loadSelected(); };
    }

    PresetBrowser::~PresetBrowser() {
        search_.removeKeyListener(this);
        stopTimer();
    }

    void PresetBrowser::opened() {
        search_.clear();
        cancelEntry();
        refresh();
        if (search_.isShowing()) {
            search_.grabKeyboardFocus();
        }
    }

    void PresetBrowser::refresh() {
        all_ = presets_.scan();
        group_rows_.clear();
        group_rows_.add("All Presets");
        group_rows_.add("Favourites");
        group_rows_.addArray(presets_.getGroups());
        group_row_ = std::clamp(group_row_, 0, group_rows_.size() - 1);
        groups_.updateContent();
        groups_.selectRow(group_row_, true, true);
        applyFilter();
    }

    void PresetBrowser::applyFilter() {
        const auto query = search_.getText().trim();
        const auto* previous = getSelected();
        const auto keep = previous != nullptr ? previous->file : presets_.getCurrentFile();
        visible_.clear();
        for (const auto& e : all_) {
            if (group_row_ == kFavouritesRow && !e.favourite) {
                continue;
            }
            if (group_row_ > kFavouritesRow && !(e.group == group_rows_[group_row_]
                                                  || e.group.startsWith(group_rows_[group_row_] + juce::File::getSeparatorString()))) {
                continue;
            }
            if (query.isNotEmpty() && !e.name.containsIgnoreCase(query) && !e.group.containsIgnoreCase(query)) {
                continue;
            }
            visible_.push_back(e);
        }
        list_.updateContent();
        const auto it = std::ranges::find_if(visible_, [&](const PresetManager::Entry& e) { return e.file == keep; });
        if (it != visible_.end()) {
            list_.selectRow(static_cast<int>(std::distance(visible_.begin(), it)));
        } else if (!visible_.empty()) {
            list_.selectRow(0);
        } else {
            list_.deselectAllRows();
        }
        updateButtons();
        repaint();
    }

    const PresetManager::Entry* PresetBrowser::getSelected() const {
        const auto row = list_.getSelectedRow();
        return row >= 0 && row < static_cast<int>(visible_.size()) ? &visible_[static_cast<size_t>(row)] : nullptr;
    }

    juce::File PresetBrowser::getTargetGroupDir() const {
        const auto dir = PresetManager::getDirectory();
        return group_row_ > kFavouritesRow ? dir.getChildFile(group_rows_[group_row_]) : dir;
    }

    void PresetBrowser::loadSelected() {
        if (const auto* selected = getSelected(); selected != nullptr && presets_.load(selected->file)) {
            close();
        }
    }

    void PresetBrowser::beginEntry(const Entry mode) {
        const auto* selected = getSelected();
        if (mode == Entry::kRename && selected == nullptr) {
            return;
        }
        entry_mode_ = mode;
        switch (mode) {
        case Entry::kSaveAs:
            entry_.setTextToShowWhenEmpty("Preset name", colour::kTextTertiary);
            entry_.setText(presets_.getCurrentFile() == juce::File() ? juce::String() : presets_.getCurrentName(), false);
            break;
        case Entry::kRename:
            entry_.setTextToShowWhenEmpty("New name", colour::kTextTertiary);
            entry_.setText(selected->name, false);
            break;
        case Entry::kNewGroup:
            entry_.setTextToShowWhenEmpty("Folder name", colour::kTextTertiary);
            entry_.clear();
            break;
        case Entry::kNone:
            break;
        }
        entry_.setVisible(true);
        updateButtons();
        resized();
        if (entry_.isShowing()) {
            entry_.grabKeyboardFocus();
        }
        entry_.selectAll();
    }

    void PresetBrowser::commitEntry() {
        const auto text = entry_.getText().trim();
        const auto mode = entry_mode_;
        if (text.isEmpty()) {
            cancelEntry();
            return;
        }
        if (mode == Entry::kSaveAs) {
            const auto file = getTargetGroupDir().getChildFile(juce::File::createLegalFileName(text) + PresetManager::kExtension);
            presets_.save(file);
        } else if (mode == Entry::kRename) {
            if (const auto* selected = getSelected()) {
                presets_.rename(selected->file, text);
            }
        } else if (mode == Entry::kNewGroup) {
            const auto dir = presets_.createGroup(text);
            refresh();
            group_row_ = group_rows_.indexOf(dir.getRelativePathFrom(PresetManager::getDirectory()));
        }
        cancelEntry();
        refresh();
    }

    void PresetBrowser::cancelEntry() {
        entry_mode_ = Entry::kNone;
        entry_.setVisible(false);
        updateButtons();
        resized();
        if (isShowing()) {
            if (search_.isShowing()) {
                search_.grabKeyboardFocus();
            }
        }
    }

    void PresetBrowser::updateButtons() {
        const auto has_selection = getSelected() != nullptr;
        const auto entering = entry_mode_ != Entry::kNone;
        delete_.setEnabled(has_selection);
        rename_.setEnabled(has_selection);
        load_.setEnabled(has_selection);
        delete_.setText(confirm_delete_ ? "Confirm Delete" : "Delete");
        delete_.setDanger(confirm_delete_);
        load_.setText(entering ? (entry_mode_ == Entry::kRename ? "Rename" : (entry_mode_ == Entry::kNewGroup ? "Create" : "Save"))
                               : "Load");
        load_.on_click = entering ? std::function<void()>([this] { commitEntry(); })
                                  : std::function<void()>([this] { loadSelected(); });
        load_.setEnabled(entering || has_selection);
        for (auto* b : {&delete_, &rename_, &save_as_}) {
            b->setVisible(!entering);
        }
        resized();
    }

    void PresetBrowser::timerCallback() {
        stopTimer();
        confirm_delete_ = false;
        updateButtons();
    }

    bool PresetBrowser::keyPressed(const juce::KeyPress& key, juce::Component*) {
        // arrow keys in the search field move through the results
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
            const auto n = static_cast<int>(visible_.size());
            if (n > 0) {
                const auto row = std::clamp(list_.getSelectedRow() + (key == juce::KeyPress::upKey ? -1 : 1), 0, n - 1);
                list_.selectRow(row);
            }
            return true;
        }
        return false;
    }

    void PresetBrowser::layoutContent(juce::Rectangle<int> area) {
        auto top = area.removeFromTop(32);
        import_.setBounds(top.removeFromRight(import_.getIdealWidth()));
        top.removeFromRight(6);
        reveal_.setBounds(top.removeFromRight(32));
        top.removeFromRight(10);
        search_.setBounds(top);
        area.removeFromTop(12);

        auto footer = area.removeFromBottom(32);
        area.removeFromBottom(12);
        load_.setBounds(footer.removeFromRight(std::max(load_.getIdealWidth(), 84)));
        footer.removeFromRight(8);
        if (entry_mode_ == Entry::kNone) {
            save_as_.setBounds(footer.removeFromRight(save_as_.getIdealWidth()));
            footer.removeFromRight(6);
            rename_.setBounds(footer.removeFromRight(rename_.getIdealWidth()));
            footer.removeFromRight(6);
            delete_.setBounds(footer.removeFromRight(std::max(delete_.getIdealWidth(), 72)));
            footer_status_ = footer;
        } else {
            entry_.setBounds(footer.removeFromRight(std::min(320, footer.getWidth())));
            footer_status_ = footer;
        }

        auto left = area.removeFromLeft(190);
        auto groups_header = left.removeFromTop(24);
        new_group_.setBounds(groups_header.removeFromRight(24));
        groups_.setBounds(left);
        area.removeFromLeft(12);
        list_.setBounds(area);
    }

    void PresetBrowser::paint(juce::Graphics& g) {
        Sheet::paint(g);
        g.setColour(colour::kTextTertiary);
        g.setFont(Fonts::get().font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f));
        g.drawText("FOLDERS", groups_.getBounds().withHeight(24).translated(0, -24).toFloat(),
                   juce::Justification::centredLeft);
        // list well
        g.setColour(colour::kGraph);
        g.fillRoundedRectangle(list_.getBounds().toFloat().expanded(4.f), size::kRadius);
        if (visible_.empty()) {
            auto r = list_.getBounds().toFloat().withSizeKeepingCentre(360.f, 44.f);
            g.setColour(colour::kTextSecondary);
            g.setFont(Fonts::get().font(type::kBody, Fonts::kSemiBold));
            const auto searching = search_.getText().isNotEmpty();
            g.drawText(searching ? "No matching presets" : (group_row_ == kFavouritesRow ? "No favourites yet" : "No presets yet"),
                       r.removeFromTop(22.f), juce::Justification::centred);
            g.setColour(colour::kTextTertiary);
            g.setFont(Fonts::get().font(type::kSmall));
            g.drawText(searching ? "Try a different name or folder"
                                 : (group_row_ == kFavouritesRow ? "Click the star next to a preset to add it here"
                                                                 : "Use Save As to store the current settings"),
                       r, juce::Justification::centred);
        }
        // status
        g.setColour(colour::kTextTertiary);
        g.setFont(Fonts::get().font(type::kSmall));
        const auto status = entry_mode_ == Entry::kSaveAs
                                ? "Save to " + (group_row_ > kFavouritesRow ? group_rows_[group_row_] : juce::String("Presets"))
                                : juce::String(static_cast<int>(visible_.size())) + (visible_.size() == 1 ? " preset" : " presets");
        g.drawText(status, footer_status_.toFloat(), juce::Justification::centredLeft);
    }

    // ---------------------------------------------------------------- models

    int PresetBrowser::GroupModel::getNumRows() {
        return owner_.group_rows_.size();
    }

    void PresetBrowser::GroupModel::paintListBoxItem(const int row, juce::Graphics& g, const int width, const int height,
                                                     const bool selected) {
        auto r = juce::Rectangle<float>(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)).reduced(0.f, 1.f);
        if (selected) {
            g.setColour(colour::kControlHover);
            g.fillRoundedRectangle(r, size::kRadiusSmall);
        }
        r.removeFromLeft(10.f);
        const auto icon = row == kFavouritesRow ? icons::Icon::kStar : icons::Icon::kFolder;
        icons::draw(g, icon, r.removeFromLeft(14.f), selected ? colour::kTextPrimary : colour::kTextTertiary, 1.2f);
        r.removeFromLeft(8.f);
        g.setColour(selected ? colour::kTextPrimary : colour::kTextSecondary);
        g.setFont(Fonts::get().font(type::kBody));
        g.drawText(owner_.group_rows_[row], r, juce::Justification::centredLeft, true);
    }

    void PresetBrowser::GroupModel::selectedRowsChanged(const int last_row) {
        if (last_row >= 0) {
            owner_.group_row_ = last_row;
            owner_.applyFilter();
        }
    }

    int PresetBrowser::PresetModel::getNumRows() {
        return static_cast<int>(owner_.visible_.size());
    }

    void PresetBrowser::PresetModel::paintListBoxItem(const int row, juce::Graphics& g, const int width, const int height,
                                                      const bool selected) {
        if (row < 0 || row >= static_cast<int>(owner_.visible_.size())) {
            return;
        }
        const auto& e = owner_.visible_[static_cast<size_t>(row)];
        auto r = juce::Rectangle<float>(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)).reduced(4.f, 1.f);
        if (selected) {
            g.setColour(colour::kAccent.withAlpha(.14f));
            g.fillRoundedRectangle(r, size::kRadiusSmall);
        }
        const auto star = r.removeFromRight(28.f);
        icons::Icon icon = icons::Icon::kStar;
        if (e.favourite) {
            auto p = icons::get(icon);
            p.applyTransform(juce::AffineTransform::scale(14.f / 16.f).translated(star.getCentreX() - 7.f, star.getCentreY() - 7.f));
            g.setColour(colour::kWarning);
            g.fillPath(p);
        } else {
            icons::draw(g, icon, star.withSizeKeepingCentre(14.f, 14.f), colour::kTextDisabled, 1.1f);
        }
        r.removeFromLeft(8.f);
        if (e.file == owner_.presets_.getCurrentFile()) {
            g.setColour(colour::kAccent);
            g.fillEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre({r.getX() + 2.5f, r.getCentreY()}));
        }
        r.removeFromLeft(14.f);
        g.setColour(colour::kTextTertiary);
        g.setFont(Fonts::get().font(type::kSmall));
        if (e.group.isNotEmpty() && owner_.group_row_ <= kFavouritesRow) {
            g.drawText(e.group, r.removeFromRight(160.f), juce::Justification::centredRight, true);
        }
        g.setColour(selected ? colour::kTextPrimary : colour::kTextSecondary);
        g.setFont(Fonts::get().font(type::kBody));
        g.drawText(e.name, r, juce::Justification::centredLeft, true);
    }

    void PresetBrowser::PresetModel::listBoxItemClicked(const int row, const juce::MouseEvent& e) {
        if (row < 0 || row >= static_cast<int>(owner_.visible_.size())) {
            return;
        }
        // the star column toggles the favourite
        if (e.x > owner_.list_.getVisibleRowWidth() - 32) {
            auto& entry = owner_.visible_[static_cast<size_t>(row)];
            entry.favourite = !entry.favourite;
            owner_.presets_.setFavourite(entry.file, entry.favourite);
            for (auto& a : owner_.all_) {
                if (a.file == entry.file) {
                    a.favourite = entry.favourite;
                }
            }
            owner_.list_.repaintRow(row);
            if (owner_.group_row_ == kFavouritesRow) {
                owner_.applyFilter();
            }
        }
    }

    void PresetBrowser::PresetModel::listBoxItemDoubleClicked(int, const juce::MouseEvent&) {
        owner_.loadSelected();
    }

    void PresetBrowser::PresetModel::returnKeyPressed(int) {
        owner_.loadSelected();
    }

    void PresetBrowser::PresetModel::deleteKeyPressed(int) {
        owner_.delete_.on_click();
    }

    void PresetBrowser::PresetModel::selectedRowsChanged(int) {
        owner_.confirm_delete_ = false;
        owner_.updateButtons();
    }
}
