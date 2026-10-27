// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.

#include "history.hpp"

#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"
#include "../util/path_minimizer.hpp"
#include "../util/value_format.hpp"

namespace altvane::ui {
    juce::Path traceHistory(const TimeAxis& time, const LevelAxis& level, const size_t count,
                            const std::function<float(size_t age)>& value, const bool peak, const bool fill) {
        juce::Path path;
        const auto n = std::min(count, time.getSpan());
        if (n < 2) {
            return path;
        }
        // one point per pixel column, newest first
        std::vector<juce::Point<float>> points;
        points.reserve(static_cast<size_t>(time.getWidth()) + 2);
        int bucket = std::numeric_limits<int>::min();
        float acc = 0.f, x_acc = 0.f;
        int members = 0;
        auto flush = [&] {
            if (members > 0) {
                const auto v = peak ? acc : acc / static_cast<float>(members);
                points.emplace_back(x_acc / static_cast<float>(members), level.dbToY(v));
            }
        };
        for (size_t age = 0; age < n; ++age) {
            const auto x = time.ageToX(static_cast<float>(age));
            const auto px = static_cast<int>(std::floor(x));
            const auto v = value(age);
            if (px != bucket) {
                flush();
                bucket = px;
                acc = v;
                x_acc = x;
                members = 1;
            } else {
                acc = peak ? std::max(acc, v) : acc + v;
                x_acc += x;
                ++members;
            }
        }
        flush();
        if (points.size() < 2) {
            return path;
        }

        const auto bottom = level.getY() + level.getHeight();
        PathMinimizer minimizer(path);
        if (fill) {
            path.startNewSubPath(points.back().x, bottom);
            minimizer.startNewSubPath<false>(points.back().x, points.back().y);
        } else {
            minimizer.startNewSubPath(points.back().x, points.back().y);
        }
        for (auto it = std::next(points.rbegin()); it != points.rend(); ++it) {
            minimizer.lineTo(it->x, it->y);
        }
        minimizer.finish();
        if (fill) {
            path.lineTo(points.front().x, bottom);
            path.closeSubPath();
        }
        return path;
    }

    void drawLevelGrid(juce::Graphics& g, const LevelAxis& level, const juce::Rectangle<float> area, const float step,
                       const juce::Rectangle<float> labels) {
        const auto first = std::ceil(level.getLow() / step) * step;
        for (auto db = first; db <= level.getHigh() + .01f; db += step) {
            const auto y = std::round(level.dbToY(db));
            g.setColour(tokens::colour::kGridMajor);
            g.fillRect(juce::Rectangle<float>(area.getX(), y, area.getWidth(), 1.f));
            if (!labels.isEmpty() && y - 6.f > area.getY() && y + 6.f < area.getBottom()) {
                const auto text = std::abs(db) < .01f ? juce::String("0")
                                                      : (db < 0.f ? format::kMinus : juce::String("+")) + juce::String(std::abs(juce::roundToInt(db)));
                g.setColour(tokens::colour::kTextTertiary);
                Fonts::get().drawTabular(g, text, labels.withY(y - 7.f).withHeight(14.f),
                                         juce::Justification::centredLeft, tokens::type::kCaption);
            }
        }
    }
}
