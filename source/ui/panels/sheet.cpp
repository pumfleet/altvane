// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "sheet.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"

namespace contour::ui {
    using namespace tokens;

    Sheet::Sheet(const juce::String& title) : title_(title) {
        setTitle(title);
        setWantsKeyboardFocus(true);
        setVisible(false);
        setAlpha(0.f);
        addAndMakeVisible(close_);
        close_.setTooltip("Close (Esc)");
        close_.on_click = [this] { close(); };
    }

    void Sheet::open() {
        if (open_) {
            return;
        }
        open_ = true;
        fade_.setTarget(true);
        setVisible(true);
        toFront(true);
        if (isShowing()) {
            grabKeyboardFocus();
        }
        opened();
    }

    void Sheet::close() {
        if (!open_) {
            return;
        }
        open_ = false;
        fade_.setTarget(false);
        if (on_closed) {
            on_closed();
        }
    }

    bool Sheet::tick(const float dt) {
        if (fade_.update(dt)) {
            const auto t = fade_.getEased();
            setAlpha(t);
            setTransform(juce::AffineTransform::translation(0.f, (1.f - t) * 8.f));
            if (!open_ && fade_.getValue() <= 0.f) {
                setVisible(false);
                setTransform({});
            }
        }
        return isVisible();
    }

    void Sheet::resized() {
        const auto size = getCardSize();
        card_ = juce::Rectangle<int>(std::min(size.x, getWidth() - 48), std::min(size.y, getHeight() - 48))
            .withCentre(getLocalBounds().getCentre());
        auto inner = card_.reduced(static_cast<int>(size::kS5), static_cast<int>(size::kS4));
        auto title_row = inner.removeFromTop(28);
        close_.setBounds(title_row.removeFromRight(28));
        inner.removeFromTop(static_cast<int>(size::kS3));
        layoutContent(inner);
    }

    void Sheet::paint(juce::Graphics& g) {
        g.fillAll(juce::Colours::black.withAlpha(.45f));
        const auto card = card_.toFloat();
        for (int i = 1; i <= 4; ++i) {
            g.setColour(juce::Colours::black.withAlpha(.06f));
            g.fillRoundedRectangle(card.expanded(static_cast<float>(i)).translated(0.f, static_cast<float>(i) * 1.5f),
                                   size::kRadiusLarge + static_cast<float>(i));
        }
        g.setColour(colour::kRaised);
        g.fillRoundedRectangle(card, size::kRadiusLarge);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(card.reduced(.5f), size::kRadiusLarge, 1.f);
        g.setColour(colour::kTextPrimary);
        g.setFont(Fonts::get().font(type::kTitle, Fonts::kSemiBold));
        g.drawText(title_, card.reduced(size::kS5, size::kS4).removeFromTop(28.f), juce::Justification::centredLeft);
    }

    void Sheet::mouseDown(const juce::MouseEvent& e) {
        if (!card_.contains(e.getPosition())) {
            close();
        }
    }

    bool Sheet::keyPressed(const juce::KeyPress& key) {
        if (key == juce::KeyPress::escapeKey) {
            close();
            return true;
        }
        return false;
    }
}
