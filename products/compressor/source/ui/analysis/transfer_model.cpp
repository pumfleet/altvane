// Copyright (C) 2026 - Altvane, based on ZLCompressor by zsliu98
// This file is part of Altvane Compressor
//
// Altvane Compressor is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Compressor is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Compressor. If not, see <https://www.gnu.org/licenses/>.
#include "transfer_model.hpp"

namespace altvane::compressor {
    bool TransferModel::sync(const CompBridge& bridge) {
        Settings s;
        s.direction = bridge.getDirection();
        s.threshold = bridge.get(zlp::PThreshold::kID);
        s.ratio = bridge.get(zlp::PRatio::kID);
        s.knee = bridge.get(zlp::PKneeW::kID);
        s.curve = bridge.get(zlp::PCurve::kID);
        s.floor = bridge.get(zlp::PFloor::kID);
        s.range = bridge.get(zlp::PRange::kID);
        s.range_inf = bridge.getBool(zlp::PRangeINF::kID);
        s.wet = bridge.get(zlp::PWet::kID);
        s.makeup = bridge.get(zlp::POutGain::kID);
        if (valid_ && s == s_) {
            return false;
        }
        setSettings(s);
        return true;
    }

    void TransferModel::setSettings(const Settings& s) {
        s_ = s;
        valid_ = true;
        compression_.setThreshold(s.threshold);
        compression_.setRatio(s.ratio);
        compression_.setKneeW(s.knee);
        compression_.setCurve(zlp::PCurve::formatV(s.curve));
        expansion_.setThreshold(s.threshold);
        expansion_.setRatio(s.ratio);
        expansion_.setKneeW(s.knee);
        expansion_.setFloor(s.floor);
        inflation_.setThreshold(s.threshold);
        inflation_.setRatio(s.ratio);
        inflation_.setKneeW(s.knee);
        inflation_.setFloor(s.floor);
        compression_.prepareBuffer();
        expansion_.prepareBuffer();
        inflation_.prepareBuffer();
    }

    float TransferModel::gainFor(const float input_db) {
        float diff = 0.f;
        switch (s_.direction) {
        case zlp::PCompDirection::kCompress:
        case zlp::PCompDirection::kShape:
            diff = compression_.eval(input_db);
            break;
        case zlp::PCompDirection::kExpand:
            diff = expansion_.eval(input_db);
            break;
        case zlp::PCompDirection::kInflate:
            diff = inflation_.eval(input_db);
            break;
        }
        // the engine scales range and gain change by wet, and raises the level for the upward directions
        const auto wet = s_.wet * .01f;
        const auto range = s_.range * wet;
        const auto low = s_.range_inf ? -240.f : -range;
        const auto high = s_.range_inf ? 40.f : std::min(40.f, range);
        const auto gain = std::clamp(diff, low, high) * wet;
        const auto downward = s_.direction == zlp::PCompDirection::kCompress
                              || s_.direction == zlp::PCompDirection::kExpand;
        return downward ? gain : -gain;
    }

    float TransferModel::outputFor(const float input_db) {
        return input_db + gainFor(input_db) + s_.makeup * s_.wet * .01f;
    }
}
