// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include "help_balloon.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"
#include "../util/spring.hpp"

namespace altvane::ui {
    using namespace tokens;

    namespace {
        const juce::Identifier kHelpTitle{"altvane_help_title"};
        const juce::Identifier kHelpText{"altvane_help_text"};
        constexpr float kWidth = 232.f, kPad = 10.f, kGap = 6.f;

        juce::TextLayout layoutText(const juce::String& title, const juce::String& text, const float width) {
            juce::AttributedString s;
            s.setWordWrap(juce::AttributedString::byWord);
            s.setLineSpacing(3.f);
            s.append(title, Fonts::get().font(type::kSmall, Fonts::kSemiBold), colour::kTextPrimary);
            if (text.isNotEmpty()) {
                s.append("\n" + text, Fonts::get().font(type::kSmall), colour::kTextSecondary);
            }
            juce::TextLayout layout;
            layout.createLayout(s, width);
            return layout;
        }
    }

    void setHelp(juce::Component& component, const juce::String& title, const juce::String& text) {
        component.getProperties().set(kHelpTitle, title);
        component.getProperties().set(kHelpText, text);
        component.setHelpText(text);
    }

    HelpBalloon::HelpBalloon(juce::Component& root) : root_(root) {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(true);
        setVisible(false);
        startTimerHz(30);
    }

    HelpBalloon::~HelpBalloon() {
        stopTimer();
    }

    void HelpBalloon::setActive(const bool active) {
        active_ = active;
        if (!active_) {
            hide();
        }
    }

    juce::Component* HelpBalloon::findHelpTarget(juce::Component* c, const juce::Component& root) {
        for (; c != nullptr; c = c->getParentComponent()) {
            if (c->getProperties().contains(kHelpTitle)) {
                return c;
            }
            if (c == &root) {
                return nullptr;
            }
        }
        return nullptr;
    }

    void HelpBalloon::hide() {
        candidate_ = nullptr;
        shown_ = nullptr;
        alpha_ = 0.f;
        setVisible(false);
    }

    void HelpBalloon::showFor(juce::Component& target) {
        title_ = target.getProperties()[kHelpTitle].toString();
        text_ = target.getProperties()[kHelpText].toString();
        shown_ = &target;
        place(target);
        alpha_ = 1.f;
        setVisible(true);
        toFront(false);
        repaint();
    }

    void HelpBalloon::place(juce::Component& target) {
        const auto layout = layoutText(title_, text_, kWidth - 2.f * kPad);
        const auto h = std::ceil(layout.getHeight() + 2.f * kPad);
        const auto w = std::ceil(std::min(kWidth, layout.getWidth() + 2.f * kPad + 1.f));
        const auto area = root_.getLocalArea(&target, target.getLocalBounds()).toFloat();
        const auto bounds = root_.getLocalBounds().toFloat().reduced(6.f);
        // below the control if it fits, otherwise above
        auto y = area.getBottom() + kGap;
        if (y + h > bounds.getBottom()) {
            y = area.getY() - kGap - h;
        }
        const auto x = std::clamp(area.getCentreX() - w * .5f, bounds.getX(), bounds.getRight() - w);
        setBounds(juce::Rectangle<float>(x, std::max(bounds.getY(), y), w, h).toNearestInt());
    }

    void HelpBalloon::timerCallback() {
        if (!active_ || !root_.isShowing()) {
            if (isVisible()) {
                hide();
            }
            return;
        }
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        auto* under = source.getComponentUnderMouse();
        auto* target = under != nullptr && (under == &root_ || root_.isParentOf(under))
                           ? findHelpTarget(under, root_)
                           : nullptr;
        if (source.isDragging() || target == nullptr) {
            if (isVisible() || candidate_ != nullptr) {
                hide();
            }
            return;
        }
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (target != candidate_.getComponent()) {
            candidate_ = target;
            candidate_since_ = now;
            if (shown_ != nullptr && shown_.getComponent() != target) {
                shown_ = nullptr;
                alpha_ = 0.f;
                setVisible(false);
            }
            return;
        }
        if (shown_ == nullptr && now - candidate_since_ >= kDelayMs) {
            showFor(*target);
            alpha_ = InteractionSettings::reduce_motion ? 1.f : 0.f;
        }
        if (shown_ != nullptr && alpha_ < 1.f) {
            alpha_ = std::min(1.f, alpha_ + .25f);
            repaint();
        }
    }

    void HelpBalloon::paint(juce::Graphics& g) {
        g.setOpacity(alpha_);
        const auto b = getLocalBounds().toFloat().reduced(.5f);
        g.setColour(juce::Colours::black.withAlpha(.35f * alpha_));
        g.fillRoundedRectangle(b.translated(0.f, 2.f), size::kRadius);
        g.setColour(colour::kOverlay.withMultipliedAlpha(alpha_));
        g.fillRoundedRectangle(b, size::kRadius);
        g.setColour(colour::kHairlineStrong.withMultipliedAlpha(alpha_));
        g.drawRoundedRectangle(b, size::kRadius, 1.f);
        auto layout = layoutText(title_, text_, kWidth - 2.f * kPad);
        layout.draw(g, b.reduced(kPad));
    }
}
