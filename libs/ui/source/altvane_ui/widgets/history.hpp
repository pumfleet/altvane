// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <array>
#include <functional>
#include <juce_graphics/juce_graphics.h>

/**
 * scrolling level displays: the newest column at the right edge, older ones to the left
 */
namespace altvane::ui {
    /**
     * the last kSize columns, owned by the editor (fed from an SpscQueue)
     */
    template <typename Column, size_t kSize>
    class HistoryRing {
    public:
        void push(const Column& column) {
            head_ = (head_ + 1) % kSize;
            columns_[head_] = column;
            count_ = std::min(count_ + 1, kSize);
        }

        /**
         * the column `age` steps back in time, 0 = newest; age < size()
         */
        const Column& at(const size_t age) const {
            return columns_[(head_ + kSize - age % kSize) % kSize];
        }

        size_t size() const { return count_; }

        void clear() { count_ = 0; }

        static constexpr size_t capacity() { return kSize; }

    private:
        std::array<Column, kSize> columns_{};
        size_t head_{0}, count_{0};
    };

    /**
     * time axis: column age (0 = newest) to x, newest at the right edge
     */
    class TimeAxis {
    public:
        void setBounds(const float x, const float width) {
            x_ = x;
            width_ = std::max(width, 1.f);
        }

        /**
         * how many columns fit across the axis (the displayed time span)
         */
        void setSpan(const size_t columns) { span_ = std::max<size_t>(columns, 2); }

        size_t getSpan() const { return span_; }

        float ageToX(const float age) const {
            return x_ + width_ - age / static_cast<float>(span_ - 1) * width_;
        }

        float xToAge(const float x) const {
            return (x_ + width_ - x) / width_ * static_cast<float>(span_ - 1);
        }

        float getX() const { return x_; }
        float getWidth() const { return width_; }

    private:
        float x_{0.f}, width_{1.f};
        size_t span_{2};
    };

    /**
     * linear dB axis from `low` (bottom) to `high` (top)
     */
    class LevelAxis {
    public:
        void setBounds(const float y, const float height) {
            y_ = y;
            height_ = std::max(height, 1.f);
        }

        void setRange(const float low, const float high) {
            low_ = low;
            high_ = std::max(high, low + .1f);
        }

        float dbToY(const float db) const {
            return y_ + (high_ - std::clamp(db, low_, high_)) / (high_ - low_) * height_;
        }

        float yToDB(const float y) const {
            return high_ - (y - y_) / height_ * (high_ - low_);
        }

        float getLow() const { return low_; }
        float getHigh() const { return high_; }
        float getY() const { return y_; }
        float getHeight() const { return height_; }

    private:
        float y_{0.f}, height_{1.f}, low_{-60.f}, high_{0.f};
    };

    /**
     * a trace through `count` columns, at most one point per pixel column: where several columns share a pixel,
     * `peak` keeps the largest value (levels), otherwise their mean (gain curves)
     * with `fill`, the path is closed down to the bottom of the level axis
     */
    juce::Path traceHistory(const TimeAxis& time, const LevelAxis& level, size_t count,
                            const std::function<float(size_t age)>& value, bool peak, bool fill);

    /**
     * horizontal grid lines every `step` dB, with labels in `labels` (left-aligned) if it is not empty
     */
    void drawLevelGrid(juce::Graphics& g, const LevelAxis& level, juce::Rectangle<float> area, float step,
                       juce::Rectangle<float> labels);
}
