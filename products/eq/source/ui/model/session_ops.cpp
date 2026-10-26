// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "session_ops.hpp"

namespace altvane::eq::session_ops {
    namespace {
        constexpr auto kClipboardTag = "ContourBands";

        juce::String fieldName(const juce::RangedAudioParameter* para) {
            // parameter IDs are "<name><band>"; strip the band index
            return para->getParameterID().trimCharactersAtEnd("0123456789");
        }

        std::vector<std::unique_ptr<juce::XmlElement>> readClipboard() {
            std::vector<std::unique_ptr<juce::XmlElement>> result;
            const auto xml = juce::parseXML(juce::SystemClipboard::getTextFromClipboard());
            if (xml == nullptr || !xml->hasTagName(kClipboardTag)) {
                return result;
            }
            for (auto* band : xml->getChildIterator()) {
                result.push_back(std::make_unique<juce::XmlElement>(*band));
            }
            return result;
        }

        std::vector<size_t> insert(EngineBridge& bridge, const std::vector<std::unique_ptr<juce::XmlElement>>& bands,
                                   const double freq_ratio, const juce::String& name) {
            std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
            std::vector<size_t> result;
            std::vector<bool> taken(kBandNum, false);
            for (const auto& band : bands) {
                size_t slot = kBandNum;
                for (size_t b = 0; b < kBandNum; ++b) {
                    if (!taken[b] && !bridge.band(b).isActive()) {
                        slot = b;
                        break;
                    }
                }
                if (slot >= kBandNum) {
                    break;
                }
                taken[slot] = true;
                result.push_back(slot);
                const auto& bp = bridge.params(slot);
                for (auto* para : bp.paras) {
                    const auto field = fieldName(para);
                    auto v = band->hasAttribute(field)
                                 ? static_cast<float>(band->getDoubleAttribute(field))
                                 : para->convertFrom0to1(para->getDefaultValue());
                    if (para == bp[BandParams::kFreq] || para == bp[BandParams::kSideFreq]) {
                        v = static_cast<float>(v * freq_ratio);
                    }
                    if (para == bp[BandParams::kStatus] && v < .5f) {
                        v = static_cast<float>(zlp::kOn);
                    }
                    values.emplace_back(para, v);
                }
            }
            if (!values.empty()) {
                bridge.setNow(values, name);
            }
            return result;
        }

        std::vector<std::unique_ptr<juce::XmlElement>> serialise(const EngineBridge& bridge,
                                                                 const std::vector<size_t>& bands) {
            std::vector<std::unique_ptr<juce::XmlElement>> result;
            for (const auto b : bands) {
                auto band = std::make_unique<juce::XmlElement>("Band");
                for (auto* para : bridge.params(b).paras) {
                    band->setAttribute(fieldName(para), para->convertFrom0to1(para->getValue()));
                }
                result.push_back(std::move(band));
            }
            return result;
        }
    }

    void copyBands(const EngineBridge& bridge, const std::vector<size_t>& bands) {
        if (bands.empty()) {
            return;
        }
        juce::XmlElement xml(kClipboardTag);
        for (auto& band : serialise(bridge, bands)) {
            xml.addChildElement(band.release());
        }
        juce::SystemClipboard::copyTextToClipboard(
            xml.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader()));
    }

    bool canPaste() {
        const auto text = juce::SystemClipboard::getTextFromClipboard().trimStart();
        return text.startsWith("<" + juce::String(kClipboardTag)) && !readClipboard().empty();
    }

    std::vector<size_t> pasteBands(EngineBridge& bridge) {
        const auto bands = readClipboard();
        return insert(bridge, bands, 1.0, bands.size() > 1 ? "Paste Bands" : "Paste Band");
    }

    std::vector<size_t> duplicateBands(EngineBridge& bridge, const std::vector<size_t>& bands) {
        return insert(bridge, serialise(bridge, bands), std::exp2(1.0 / 3.0),
                      bands.size() > 1 ? "Duplicate Bands" : "Duplicate Band");
    }

    int getABSlot(const EngineBridge& bridge) {
        return bridge.getProcessor().ab_slot_;
    }

    void switchAB(EngineBridge& bridge) {
        if (bridge.isEditing()) {
            bridge.endEdit();
        }
        auto& p = bridge.getProcessor();
        const auto current = std::clamp(p.ab_slot_, 0, 1);
        const auto other = 1 - current;
        p.ab_states_[static_cast<size_t>(current)] = p.parameters_.copyState();
        if (!p.ab_states_[static_cast<size_t>(other)].isValid()) {
            p.ab_states_[static_cast<size_t>(other)] = p.ab_states_[static_cast<size_t>(current)].createCopy();
        }
        p.parameters_.replaceState(p.ab_states_[static_cast<size_t>(other)].createCopy());
        p.ab_slot_ = other;
        bridge.getUndoManager().clearUndoHistory();
    }

    void copyToOtherSlot(EngineBridge& bridge) {
        auto& p = bridge.getProcessor();
        p.ab_states_[static_cast<size_t>(1 - std::clamp(p.ab_slot_, 0, 1))] = p.parameters_.copyState();
    }
}
