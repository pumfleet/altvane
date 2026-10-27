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
#include <model_data.hpp>

namespace altvane::dialogue::dsp {
    static_assert(DpdfNet::kBins == kBins);

    SeparatorModel::SeparatorModel(Token, const std::span<const std::byte> onnx, juce::String name) :
        network_(onnx),
        name_(std::move(name)) {
    }

    std::shared_ptr<SeparatorModel> SeparatorModel::get(const Quality quality) {
        static std::mutex mutex;
        static std::array<std::weak_ptr<SeparatorModel>, 2> cache;
        const std::scoped_lock lock(mutex);
        auto& slot = cache[quality == Quality::kHigh ? 1 : 0];
        if (auto model = slot.lock()) {
            return model;
        }
        try {
            auto model = quality == Quality::kHigh
                             ? std::make_shared<SeparatorModel>(Token{}, models::dpdfnet8(), "dpdfnet8_48khz_hr")
                             : std::make_shared<SeparatorModel>(Token{}, models::dpdfnet2(), "dpdfnet2_48khz_hr");
            slot = model;
            return model;
        } catch (const std::exception& e) {
            DBG("Altvane Dialogue: could not load the model: " << e.what());
            return nullptr;
        }
    }

    SeparatorStreams::SeparatorStreams(std::shared_ptr<SeparatorModel> model, const int count) :
        model_(std::move(model)) {
        jassert(count >= 1 && count <= SeparatorModel::kMaxStreams);
        for (int i = 0; i < count; ++i) {
            states_.push_back(model_->getNetwork().makeState());
        }
    }

    void SeparatorStreams::reset() {
        for (auto& s : states_) {
            model_->getNetwork().reset(s);
        }
    }

    void SeparatorStreams::process(const std::span<const Spectrum* const> in, const std::span<Spectrum* const> speech,
                                   SeparatorScratch& scratch) {
        jassert(in.size() == states_.size() && speech.size() == states_.size());
        // std::complex<float> is laid out as float[2], the model's (re, im) pairs
        std::array<const float*, SeparatorModel::kMaxStreams> ins{};
        std::array<float*, SeparatorModel::kMaxStreams> outs{};
        std::array<DpdfNet::State*, SeparatorModel::kMaxStreams> states{};
        for (size_t i = 0; i < states_.size(); ++i) {
            ins[i] = reinterpret_cast<const float*>(in[i]->data());
            outs[i] = reinterpret_cast<float*>(speech[i]->data());
            states[i] = &states_[i];
        }
        model_->getNetwork().run(size(), ins.data(), states.data(), outs.data(), scratch);
    }
}
