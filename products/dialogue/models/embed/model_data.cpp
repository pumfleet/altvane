// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "model_data.hpp"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"

namespace {
    alignas(64) constexpr unsigned char kDpdfnet2[] = {
#embed "dpdfnet2_48khz_hr.onnx"
    };

    alignas(64) constexpr unsigned char kDpdfnet8[] = {
#embed "dpdfnet8_48khz_hr.onnx"
    };
}

#pragma clang diagnostic pop

namespace altvane::dialogue::models {
    std::span<const std::byte> dpdfnet2() {
        return std::as_bytes(std::span(kDpdfnet2));
    }

    std::span<const std::byte> dpdfnet8() {
        return std::as_bytes(std::span(kDpdfnet8));
    }
}
