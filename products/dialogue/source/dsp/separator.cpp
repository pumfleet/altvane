// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "separator.hpp"

#include <mutex>
#include <onnxruntime_cxx_api.h>
#include <model_data.hpp>

namespace altvane::dialogue::dsp {
    struct SeparatorModel::Impl {
        Ort::Session session{nullptr};
        Ort::MemoryInfo memory{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
        std::array<std::string, 2> input_names, output_names;
        std::array<const char*, 2> inputs{}, outputs{};
        std::array<int64_t, 4> spec_shape{1, 1, kBins, 2};
        std::array<int64_t, 1> state_shape{0};
    };

    struct ModelLoader {
        static Ort::Env& env() {
            static Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "AltvaneDialogue"};
            return env;
        }

        static std::vector<float> parseFloats(const std::string& text) {
            std::vector<float> values;
            for (const auto& token : juce::StringArray::fromTokens(juce::String(text), ",", "")) {
                values.push_back(token.trim().getFloatValue());
            }
            return values;
        }

        static std::shared_ptr<SeparatorModel> load(const Quality quality) {
            struct Model final : SeparatorModel {
            };
            auto model = std::make_shared<Model>();
            SeparatorModel& base = *model;
            base.impl_ = std::make_unique<SeparatorModel::Impl>();
            auto& impl = *base.impl_;
            const auto data = quality == Quality::kHigh ? models::dpdfnet8() : models::dpdfnet2();
            base.name_ = quality == Quality::kHigh ? "dpdfnet8_48khz_hr" : "dpdfnet2_48khz_hr";

            Ort::SessionOptions options;
            // one thread, no spinning: the worker thread is the only one that should run
            options.SetIntraOpNumThreads(1);
            options.SetInterOpNumThreads(1);
            options.SetExecutionMode(ORT_SEQUENTIAL);
            options.AddConfigEntry("session.intra_op.allow_spinning", "0");
            options.AddConfigEntry("session.inter_op.allow_spinning", "0");
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            impl.session = Ort::Session(env(), data.data(), data.size(), options);

            Ort::AllocatorWithDefaultOptions allocator;
            for (size_t i = 0; i < 2; ++i) {
                impl.input_names[i] = impl.session.GetInputNameAllocated(i, allocator).get();
                impl.output_names[i] = impl.session.GetOutputNameAllocated(i, allocator).get();
                impl.inputs[i] = impl.input_names[i].c_str();
                impl.outputs[i] = impl.output_names[i].c_str();
            }

            // the recurrent state starts as zeros, except for the running normalisation statistics
            const auto meta = impl.session.GetModelMetadata();
            auto lookup = [&](const char* key) {
                const auto value = meta.LookupCustomMetadataMapAllocated(key, allocator);
                if (value == nullptr) {
                    throw std::runtime_error(std::string("model metadata is missing ") + key);
                }
                return std::string(value.get());
            };
            const auto state_size = static_cast<size_t>(std::stoul(lookup("state_size")));
            const auto erb_size = static_cast<size_t>(std::stoul(lookup("erb_norm_state_size")));
            const auto spec_size = static_cast<size_t>(std::stoul(lookup("spec_norm_state_size")));
            const auto erb_init = parseFloats(lookup("erb_norm_init"));
            const auto spec_init = parseFloats(lookup("spec_norm_init"));
            if (erb_init.size() != erb_size || spec_init.size() != spec_size || erb_size + spec_size > state_size
                || std::stoi(lookup("n_fft")) != kFftSize || std::stoi(lookup("hop_length")) != kHop) {
                throw std::runtime_error("unexpected model layout");
            }
            base.initial_state_.assign(state_size, 0.f);
            std::copy(erb_init.begin(), erb_init.end(), base.initial_state_.begin());
            std::copy(spec_init.begin(), spec_init.end(), base.initial_state_.begin() + static_cast<long>(erb_size));
            impl.state_shape[0] = static_cast<int64_t>(state_size);
            return model;
        }
    };

    std::shared_ptr<SeparatorModel> SeparatorModel::get(const Quality quality) {
        static std::mutex mutex;
        static std::array<std::weak_ptr<SeparatorModel>, 2> cache;
        const std::scoped_lock lock(mutex);
        auto& slot = cache[quality == Quality::kHigh ? 1 : 0];
        if (auto model = slot.lock()) {
            return model;
        }
        try {
            auto model = ModelLoader::load(quality);
            slot = model;
            return model;
        } catch (const std::exception& e) {
            DBG("Altvane Dialogue: could not load the model: " << e.what());
            return nullptr;
        }
    }

    bool SeparatorModel::run(const float* spec_in, const float* state_in, float* spec_out, float* state_out) const {
        auto& impl = *impl_;
        const auto state_count = static_cast<size_t>(impl.state_shape[0]);
        try {
            std::array<Ort::Value, 2> inputs{
                Ort::Value::CreateTensor<float>(impl.memory, const_cast<float*>(spec_in), kBins * 2,
                                                impl.spec_shape.data(), impl.spec_shape.size()),
                Ort::Value::CreateTensor<float>(impl.memory, const_cast<float*>(state_in), state_count,
                                                impl.state_shape.data(), impl.state_shape.size())
            };
            std::array<Ort::Value, 2> outputs{
                Ort::Value::CreateTensor<float>(impl.memory, spec_out, kBins * 2,
                                                impl.spec_shape.data(), impl.spec_shape.size()),
                Ort::Value::CreateTensor<float>(impl.memory, state_out, state_count,
                                                impl.state_shape.data(), impl.state_shape.size())
            };
            impl.session.Run(Ort::RunOptions{nullptr}, impl.inputs.data(), inputs.data(), inputs.size(),
                             impl.outputs.data(), outputs.data(), outputs.size());
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    SeparatorStream::SeparatorStream(std::shared_ptr<SeparatorModel> model) :
        model_(std::move(model)),
        state_a_(model_->getInitialState()),
        state_b_(model_->getStateSize(), 0.f) {
    }

    void SeparatorStream::reset() {
        state_a_ = model_->getInitialState();
        a_is_input_ = true;
    }

    bool SeparatorStream::process(const std::span<const Complex, kBins> in, const std::span<Complex, kBins> speech) {
        // std::complex<float> is laid out as float[2], the model's (re, im) pairs
        auto* state_in = a_is_input_ ? state_a_.data() : state_b_.data();
        auto* state_out = a_is_input_ ? state_b_.data() : state_a_.data();
        if (!model_->run(reinterpret_cast<const float*>(in.data()), state_in, spec_out_.data(), state_out)) {
            return false;
        }
        a_is_input_ = !a_is_input_;
        for (size_t k = 0; k < kBins; ++k) {
            speech[k] = {spec_out_[2 * k], spec_out_[2 * k + 1]};
        }
        return true;
    }
}
