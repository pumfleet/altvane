// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "engine_bridge.hpp"

namespace contour::ui::band_ops {
    /**
     * the filter type a new band gets when created at a frequency
     * near the edges of the spectrum it becomes a cut, with alt a shelf
     */
    zldsp::filter::FilterType inferType(double freq, bool shelf_modifier);

    /**
     * create a band in the first free slot, every other band parameter reset to default
     * @return the band index, or kBandNum if all bands are in use
     */
    size_t add(EngineBridge& bridge, double freq, float gain, zldsp::filter::FilterType type);

    void remove(EngineBridge& bridge, const std::vector<size_t>& bands);

    void toggleBypass(EngineBridge& bridge, const std::vector<size_t>& bands);

    void setDynamic(EngineBridge& bridge, size_t band, bool on);

    void setType(EngineBridge& bridge, size_t band, zldsp::filter::FilterType type);

    void setOrder(EngineBridge& bridge, size_t band, size_t order_idx);

    void setStereo(EngineBridge& bridge, size_t band, zlp::FilterStereo lr);

    /**
     * the dB value a node is drawn at: shelves sit at half gain like the classic Pro-Q layout
     */
    float getNodeDB(const BandSnapshot& b, bool target = false);

    /**
     * convert a node's dragged dB back into the gain parameter
     */
    float nodeDBToGain(const BandSnapshot& b, float node_db);

    juce::String typeName(zldsp::filter::FilterType type);
}
