// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace altvane::dialogue::dsp {
    /**
     * the parts of an ONNX model that a hand-written network needs: its float tensors (initializers and Constant
     * nodes), each node's inputs by node name, and the metadata; read straight from the protobuf wire format
     */
    class OnnxFile {
    public:
        struct Tensor {
            std::vector<int64_t> dims;
            std::vector<float> data;
        };

        /**
         * @throw std::runtime_error if the data is not an ONNX model this reader understands
         */
        explicit OnnxFile(std::span<const std::byte> data);

        /**
         * a float tensor that is input `index` of the node called `node`
         * @throw std::runtime_error if there is no such node, input or float tensor
         */
        const Tensor& input(const std::string& node, size_t index) const;

        bool hasNode(const std::string& node) const { return nodes_.contains(node); }

        /**
         * @throw std::runtime_error if the key is missing
         */
        const std::string& metadata(const std::string& key) const;

    private:
        std::map<std::string, Tensor> tensors_;
        std::map<std::string, std::vector<std::string>> nodes_;
        std::map<std::string, std::string> metadata_;
    };
}
