// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "onnx_file.hpp"

#include <cstring>
#include <stdexcept>

namespace altvane::dialogue::dsp {
    namespace {
        // protobuf wire format: each field is a varint key (number << 3 | wire type) followed by its value
        enum WireType { kVarint = 0, kFixed64 = 1, kBytes = 2, kFixed32 = 5 };

        // field numbers, from onnx.proto
        namespace model { constexpr uint64_t kGraph = 7, kMetadata = 14; }
        namespace graph { constexpr uint64_t kNode = 1, kInitializer = 5; }
        namespace node { constexpr uint64_t kInput = 1, kOutput = 2, kName = 3, kOpType = 4, kAttribute = 5; }
        namespace attribute { constexpr uint64_t kName = 1, kTensor = 5; }
        namespace tensor { constexpr uint64_t kDims = 1, kDataType = 2, kFloatData = 4, kName = 8, kRawData = 9; }
        namespace entry { constexpr uint64_t kKey = 1, kValue = 2; }
        constexpr uint64_t kFloat = 1;

        struct Reader {
            const std::byte* pos;
            const std::byte* end;

            explicit Reader(const std::span<const std::byte> data) : pos(data.data()), end(data.data() + data.size()) {
            }

            bool done() const { return pos >= end; }

            uint64_t varint() {
                uint64_t value = 0;
                for (int shift = 0; shift < 64; shift += 7) {
                    if (pos >= end) {
                        throw std::runtime_error("truncated ONNX data");
                    }
                    const auto b = static_cast<uint8_t>(*pos++);
                    value |= static_cast<uint64_t>(b & 0x7f) << shift;
                    if ((b & 0x80) == 0) {
                        return value;
                    }
                }
                throw std::runtime_error("bad varint in ONNX data");
            }

            std::span<const std::byte> bytes() {
                const auto size = varint();
                if (size > static_cast<uint64_t>(end - pos)) {
                    throw std::runtime_error("truncated ONNX data");
                }
                const std::span<const std::byte> out(pos, static_cast<size_t>(size));
                pos += size;
                return out;
            }

            std::string string() {
                const auto b = bytes();
                return {reinterpret_cast<const char*>(b.data()), b.size()};
            }

            void skip(const uint64_t wire) {
                switch (wire) {
                    case kVarint: varint();
                        break;
                    case kFixed64: advance(8);
                        break;
                    case kBytes: bytes();
                        break;
                    case kFixed32: advance(4);
                        break;
                    default: throw std::runtime_error("unsupported protobuf wire type in ONNX data");
                }
            }

            void advance(const size_t n) {
                if (n > static_cast<size_t>(end - pos)) {
                    throw std::runtime_error("truncated ONNX data");
                }
                pos += n;
            }

            /**
             * calls f(field, wire type) for every field; f must consume the value or return false to skip it
             */
            template <typename F>
            void fields(F&& f) {
                while (!done()) {
                    const auto key = varint();
                    if (!f(key >> 3, key & 7)) {
                        skip(key & 7);
                    }
                }
            }
        };

        void appendFloats(const std::span<const std::byte> b, std::vector<float>& out) {
            const auto n = b.size() / sizeof(float);
            const auto first = out.size();
            out.resize(first + n);
            std::memcpy(out.data() + first, b.data(), n * sizeof(float)); // ONNX stores little-endian floats
        }

        /**
         * @return false if the tensor is not a float tensor
         */
        bool parseTensor(const std::span<const std::byte> data, std::string& name, OnnxFile::Tensor& t) {
            uint64_t type = 0;
            Reader r(data);
            r.fields([&](const uint64_t field, const uint64_t wire) {
                if (field == tensor::kDims && wire == kVarint) {
                    t.dims.push_back(static_cast<int64_t>(r.varint()));
                } else if (field == tensor::kDims && wire == kBytes) {
                    Reader packed(r.bytes());
                    while (!packed.done()) {
                        t.dims.push_back(static_cast<int64_t>(packed.varint()));
                    }
                } else if (field == tensor::kDataType && wire == kVarint) {
                    type = r.varint();
                } else if (field == tensor::kFloatData && wire == kBytes) {
                    appendFloats(r.bytes(), t.data);
                } else if (field == tensor::kFloatData && wire == kFixed32) {
                    appendFloats({r.pos, 4}, t.data);
                    r.advance(4);
                } else if (field == tensor::kName && wire == kBytes) {
                    name = r.string();
                } else if (field == tensor::kRawData && wire == kBytes) {
                    appendFloats(r.bytes(), t.data);
                } else {
                    return false;
                }
                return true;
            });
            size_t count = 1;
            for (const auto d : t.dims) {
                count *= static_cast<size_t>(d);
            }
            return type == kFloat && count == t.data.size();
        }
    }

    OnnxFile::OnnxFile(const std::span<const std::byte> data) {
        std::span<const std::byte> graph_data;
        Reader r(data);
        r.fields([&](const uint64_t field, const uint64_t wire) {
            if (field == model::kGraph && wire == kBytes) {
                graph_data = r.bytes();
            } else if (field == model::kMetadata && wire == kBytes) {
                std::string key, value;
                Reader e(r.bytes());
                e.fields([&](const uint64_t f, const uint64_t w) {
                    if (w != kBytes || (f != entry::kKey && f != entry::kValue)) {
                        return false;
                    }
                    (f == entry::kKey ? key : value) = e.string();
                    return true;
                });
                metadata_[key] = value;
            } else {
                return false;
            }
            return true;
        });
        if (graph_data.empty()) {
            throw std::runtime_error("ONNX data has no graph");
        }

        Reader g(graph_data);
        g.fields([&](const uint64_t field, const uint64_t wire) {
            if (wire != kBytes) {
                return false;
            }
            if (field == graph::kInitializer) {
                std::string name;
                Tensor t;
                if (parseTensor(g.bytes(), name, t)) {
                    tensors_[name] = std::move(t);
                }
            } else if (field == graph::kNode) {
                std::string name, op;
                std::vector<std::string> inputs, outputs;
                std::span<const std::byte> value;
                Reader n(g.bytes());
                n.fields([&](const uint64_t f, const uint64_t w) {
                    if (w != kBytes) {
                        return false;
                    }
                    switch (f) {
                        case node::kInput: inputs.push_back(n.string());
                            break;
                        case node::kOutput: outputs.push_back(n.string());
                            break;
                        case node::kName: name = n.string();
                            break;
                        case node::kOpType: op = n.string();
                            break;
                        case node::kAttribute: {
                            std::string attr_name;
                            std::span<const std::byte> attr_tensor;
                            Reader a(n.bytes());
                            a.fields([&](const uint64_t af, const uint64_t aw) {
                                if (aw == kBytes && af == attribute::kName) {
                                    attr_name = a.string();
                                } else if (aw == kBytes && af == attribute::kTensor) {
                                    attr_tensor = a.bytes();
                                } else {
                                    return false;
                                }
                                return true;
                            });
                            if (attr_name == "value") {
                                value = attr_tensor;
                            }
                            break;
                        }
                        default: return false;
                    }
                    return true;
                });
                // a Constant node's value is a tensor named after its output
                if (op == "Constant" && outputs.size() == 1 && !value.empty()) {
                    std::string ignored;
                    Tensor t;
                    if (parseTensor(value, ignored, t)) {
                        tensors_[outputs[0]] = std::move(t);
                    }
                }
                if (!name.empty()) {
                    nodes_[name] = std::move(inputs);
                }
            } else {
                return false;
            }
            return true;
        });
    }

    const OnnxFile::Tensor& OnnxFile::input(const std::string& node, const size_t index) const {
        const auto n = nodes_.find(node);
        if (n == nodes_.end() || index >= n->second.size()) {
            throw std::runtime_error("ONNX model has no input " + std::to_string(index) + " on node " + node);
        }
        const auto t = tensors_.find(n->second[index]);
        if (t == tensors_.end()) {
            throw std::runtime_error("ONNX model has no float tensor for input " + std::to_string(index) + " of " + node);
        }
        return t->second;
    }

    const std::string& OnnxFile::metadata(const std::string& key) const {
        const auto it = metadata_.find(key);
        if (it == metadata_.end()) {
            throw std::runtime_error("ONNX model metadata is missing " + key);
        }
        return it->second;
    }
}
