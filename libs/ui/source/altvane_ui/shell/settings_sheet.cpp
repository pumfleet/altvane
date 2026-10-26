// Copyright (C) 2026 - Altvane, based on ZLEqualizer by zsliu98
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "settings_sheet.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"

namespace altvane::ui {
    using namespace tokens;

    namespace {
        // accent swatches are drawn over icon buttons that carry no icon of their own
        class SwatchPainter {
        public:
            static void paint(juce::Graphics& g, const juce::Rectangle<float> r, const juce::Colour c, const bool selected) {
                const auto dot = juce::Rectangle<float>(16.f, 16.f).withCentre(r.getCentre());
                if (selected) {
                    g.setColour(c.withAlpha(.35f));
                    g.drawEllipse(dot.expanded(4.f), 2.f);
                }
                g.setColour(c);
                g.fillEllipse(dot);
            }
        };
    }

    SettingsSheet::SettingsSheet() : Sheet("Settings") {
        for (size_t i = 0; i < scales_.size(); ++i) {
            auto& chip = scales_[i];
            chip.setText(juce::String(juce::roundToInt(UISettings::kScales[i] * 100.f)) + "%");
            chip.on_click = [this, i] {
                settings_.scale = UISettings::kScales[i];
                changed();
            };
            addAndMakeVisible(chip);
        }
        for (size_t i = 0; i < accents_.size(); ++i) {
            auto& b = accents_[i];
            b.setTooltip(colour::kAccentChoices[i].first);
            b.on_click = [this, i] {
                settings_.accent = static_cast<int>(i);
                changed();
            };
            // the button only provides hit testing and hover; the swatch is painted by the sheet
            b.setAlpha(0.f);
            addAndMakeVisible(b);
        }
        reduce_motion_.on_change = [this](const bool on) {
            settings_.reduce_motion = on;
            changed();
        };
        for (auto* f : {&drag_, &wheel_}) {
            f->setRange(juce::NormalisableRange<float>(.25f, 4.f, .05f, .5f), 1.f);
            f->format = [](const float v) { return juce::String(juce::roundToInt(v * 100.f)) + "%"; };
            f->parse = [](const juce::String& t) -> std::optional<float> {
                const auto v = t.retainCharacters("0123456789.").getFloatValue();
                return v > 0.f ? std::optional<float>{v / 100.f} : std::nullopt;
            };
            addAndMakeVisible(f);
        }
        drag_.setFramed(true);
        wheel_.setFramed(true);
        drag_.on_change = [this](const float v) {
            settings_.drag_sensitivity = v;
            changed();
        };
        wheel_.on_change = [this](const float v) {
            settings_.wheel_sensitivity = v;
            changed();
        };
        reset_size_.on_click = [this] {
            if (on_reset_window_size) {
                on_reset_window_size();
            }
        };
        addAndMakeVisible(reduce_motion_);
        addAndMakeVisible(reset_size_);
    }

    void SettingsSheet::addSwitch(const juce::String& section, const juce::String& label, const juce::String& key) {
        auto control = std::make_unique<Switch>();
        control->on_change = [this, key](const bool on) {
            settings_.switches[key] = on;
            changed();
        };
        addAndMakeVisible(*control);
        switches_.push_back({section, label, key, std::move(control)});
    }

    void SettingsSheet::setSettings(const UISettings& settings) {
        settings_ = settings;
        sync();
    }

    void SettingsSheet::setStatus(const juce::String& status) {
        if (status != status_) {
            status_ = status;
            repaint(about_);
        }
    }

    void SettingsSheet::changed() {
        sync();
        if (on_change) {
            on_change(settings_);
        }
        repaint();
    }

    void SettingsSheet::sync() {
        for (size_t i = 0; i < scales_.size(); ++i) {
            scales_[i].setOn(std::abs(UISettings::kScales[i] - settings_.scale) < 1e-3f);
        }
        reduce_motion_.setOn(settings_.reduce_motion);
        for (auto& row : switches_) {
            row.control->setOn(settings_.getSwitch(row.key));
        }
        drag_.setValue(settings_.drag_sensitivity);
        wheel_.setValue(settings_.wheel_sensitivity);
        repaint();
    }

    void SettingsSheet::layoutContent(juce::Rectangle<int> area) {
        labels_.clear();
        sections_.clear();
        constexpr int kRow = 34;
        constexpr int kLabelW = 170;
        auto section = [&](const juce::String& name) {
            sections_.emplace_back(name, area.getY());
            area.removeFromTop(22);
        };
        auto row = [&](const juce::String& label) {
            auto r = area.removeFromTop(kRow);
            labels_.emplace_back(label, r.removeFromLeft(kLabelW));
            return r;
        };

        section("INTERFACE");
        {
            auto r = row("Size");
            for (auto& chip : scales_) {
                chip.setBounds(r.removeFromLeft(48).reduced(1, 5));
            }
        }
        {
            auto r = row("Accent");
            for (auto& b : accents_) {
                b.setBounds(r.removeFromLeft(30).reduced(1, 3));
            }
        }
        reduce_motion_.setBounds(row("Reduce motion").removeFromLeft(40));
        {
            auto r = row("Window");
            reset_size_.setBounds(r.removeFromLeft(reset_size_.getIdealWidth()).reduced(0, 3));
        }
        area.removeFromTop(10);

        section("INPUT");
        drag_.setBounds(row("Knob drag speed").removeFromLeft(92).reduced(0, 5));
        wheel_.setBounds(row("Scroll speed").removeFromLeft(92).reduced(0, 5));
        area.removeFromTop(10);

        juce::String current_section;
        for (auto& r : switches_) {
            if (r.section != current_section) {
                if (current_section.isNotEmpty()) {
                    area.removeFromTop(10);
                }
                current_section = r.section;
                section(r.section);
            }
            r.control->setBounds(row(r.label).removeFromLeft(40));
        }
        if (current_section.isNotEmpty()) {
            area.removeFromTop(10);
        }

        section("ABOUT");
        about_ = area.removeFromTop(64);
    }

    void SettingsSheet::paint(juce::Graphics& g) {
        Sheet::paint(g);
        auto& fonts = Fonts::get();
        const auto card_inner = card_.reduced(static_cast<int>(size::kS5), 0);
        g.setFont(fonts.font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f));
        for (const auto& [name, y] : sections_) {
            g.setColour(colour::kTextTertiary);
            g.drawText(name, juce::Rectangle<int>(card_inner.getX(), y, 200, 16).toFloat(), juce::Justification::centredLeft);
            g.setColour(colour::kHairline);
            g.fillRect(juce::Rectangle<float>(static_cast<float>(card_inner.getX()) + 80.f, static_cast<float>(y) + 8.f,
                                              static_cast<float>(card_inner.getWidth()) - 80.f, 1.f));
        }
        g.setFont(fonts.font(type::kBody));
        g.setColour(colour::kTextSecondary);
        for (const auto& [label, r] : labels_) {
            g.drawText(label, r.toFloat(), juce::Justification::centredLeft);
        }
        for (size_t i = 0; i < accents_.size(); ++i) {
            SwatchPainter::paint(g, accents_[i].getBounds().toFloat(), colour::kAccentChoices[i].second,
                                 static_cast<int>(i) == settings_.accent);
        }
        g.setColour(colour::kTextPrimary);
        g.setFont(fonts.font(type::kBody, Fonts::kSemiBold));
        auto about = about_.toFloat().withHeight(48.f);
        g.drawText(juce::String(PRODUCT_NAME_WITHOUT_VERSION) + " " + juce::String(VERSION), about.removeFromTop(about.getHeight() * .5f),
                   juce::Justification::bottomLeft);
        g.setColour(colour::kTextTertiary);
        g.setFont(fonts.font(type::kSmall));
        g.drawText(credit_, about.translated(0.f, 2.f), juce::Justification::topLeft);
        if (status_.isNotEmpty()) {
            g.drawText(status_, about.translated(0.f, 18.f), juce::Justification::topLeft);
        }
    }
}
