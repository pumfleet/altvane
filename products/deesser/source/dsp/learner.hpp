// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <algorithm>
#include <optional>
#include <vector>

namespace altvane::deesser::dsp {
    /**
     * auto threshold: collects the peak levels of the esses it hears, then places the threshold so a typical
     * ess (the median) is turned down by kTypicalReductionDb and louder ones by more (up to the range)
     */
    class ThresholdLearner {
    public:
        static constexpr float kTypicalReductionDb = 4.f;
        static constexpr size_t kEnough = 16;

        void start() {
            peaks_.clear();
            peaks_.reserve(kEnough);
            learning_ = true;
        }

        void cancel() { learning_ = false; }

        bool isLearning() const { return learning_; }

        /**
         * @return true once enough esses have been heard
         */
        bool add(const float peak_db) {
            if (learning_ && peaks_.size() < kEnough) {
                peaks_.push_back(peak_db);
            }
            return peaks_.size() >= kEnough;
        }

        size_t count() const { return peaks_.size(); }

        /**
         * ends learning; the threshold, if at least three esses were heard
         */
        std::optional<float> finish(const float min_db, const float max_db) {
            learning_ = false;
            if (peaks_.size() < 3) {
                return std::nullopt;
            }
            auto sorted = peaks_;
            const auto mid = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
            std::nth_element(sorted.begin(), mid, sorted.end());
            return std::clamp(*mid - kTypicalReductionDb, min_db, max_db);
        }

    private:
        std::vector<float> peaks_;
        bool learning_{false};
    };
}
