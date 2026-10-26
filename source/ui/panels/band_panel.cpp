// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include "band_panel.hpp"

#include "../model/band_ops.hpp"
#include "../theme/fonts.hpp"
#include "../theme/tokens.hpp"
#include "../util/value_format.hpp"

namespace contour::ui {
    using namespace tokens;
    using zldsp::filter::FilterType;

    namespace {
        constexpr float kHeaderH = 30.f;
        constexpr float kBodyH = 88.f;
        constexpr float kDynamicH = 84.f;
        constexpr float kSideH = 40.f;
        constexpr float kKnobW = 68.f;

        const std::array<const char*, 5> kStereoNames{"Stereo", "Left", "Right", "Mid", "Side"};
        const std::array<const char*, 4> kSideTypeNames{"Band", "Low", "High", "Full"};

        std::optional<float> parseGain(const juce::String& t) {
            if (const auto v = format::parseNumber(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        }
    }

    BandPanel::BandPanel(EngineBridge& bridge, Selection& selection) : bridge_(bridge), selection_(selection) {
        for (auto* c : std::initializer_list<juce::Component*>{
                 &prev_, &next_, &dynamic_, &solo_, &bypass_, &delete_, &shape_, &slope_, &stereo_,
                 &freq_, &gain_, &q_, &range_, &threshold_, &knee_, &attack_, &release_, &learn_, &relative_,
                 &side_type_, &side_slope_, &side_freq_, &side_q_, &link_, &swap_, &side_solo_
             }) {
            addAndMakeVisible(c);
        }
        prev_.setTooltip("Previous band (Shift+Tab)");
        next_.setTooltip("Next band (Tab)");
        dynamic_.setTooltip("Dynamic EQ");
        solo_.setTooltip("Solo: hold to listen, click to latch (S)");
        bypass_.setTooltip("Bypass band (B)");
        delete_.setTooltip("Delete band (Delete)");
        prev_.on_click = [this] { step(false); };
        next_.on_click = [this] { step(true); };
        dynamic_.on_click = [this] {
            if (band_ < kBandNum) {
                band_ops::setDynamic(bridge_, band_, !bridge_.band(band_).dynamic_on);
            }
        };
        bypass_.on_click = [this] { band_ops::toggleBypass(bridge_, targets()); };
        delete_.on_click = [this] { band_ops::remove(bridge_, targets()); };
        solo_.on_press = [this] {
            if (band_ >= kBandNum || !on_solo) {
                return;
            }
            if (solo_latched_) {
                solo_latched_ = false;
                on_solo(kBandNum, false);
                return;
            }
            solo_press_time_ = juce::Time::getMillisecondCounterHiRes();
            on_solo(band_, false);
        };
        solo_.on_release = [this] {
            if (!on_solo || !get_solo || get_solo() >= kBandNum) {
                return;
            }
            const auto held = juce::Time::getMillisecondCounterHiRes() - solo_press_time_;
            if (held < 250.0) {
                solo_latched_ = true;
            } else {
                on_solo(kBandNum, false);
            }
        };
        side_solo_.setTooltip("Listen to the detector: hold, or click to latch");
        side_solo_.on_press = [this] {
            if (band_ >= kBandNum || !on_solo) {
                return;
            }
            if (solo_latched_) {
                solo_latched_ = false;
                on_solo(kBandNum, false);
                return;
            }
            solo_press_time_ = juce::Time::getMillisecondCounterHiRes();
            on_solo(band_, true);
        };
        side_solo_.on_release = solo_.on_release;

        side_type_.setLabel("Detector");
        side_type_.build_menu = [this] {
            juce::PopupMenu menu;
            menu.addSectionHeader("Detector Filter");
            const auto current = band_ < kBandNum ? static_cast<int>(bridge_.band(band_).side_type) : -1;
            for (int t = 0; t < 4; ++t) {
                menu.addItem(t + 1, kSideTypeNames[static_cast<size_t>(t)], true, t == current);
            }
            return menu;
        };
        side_type_.on_result = [this](const int r) {
            const auto& bp = bridge_.params(band_);
            bridge_.setNow({{bp[BandParams::kSideType], static_cast<float>(r - 1)}, {bp[BandParams::kSideLink], 0.f}},
                           "Change Detector Filter");
        };
        side_slope_.setLabel("Detector Slope");
        side_slope_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = band_ < kBandNum ? static_cast<int>(bridge_.band(band_).side_order_idx) : -1;
            for (int o = 0; o < zlp::PSideOrder::kChoices.size(); ++o) {
                menu.addItem(o + 1, zlp::PSideOrder::kChoices[o], true, o == current);
            }
            return menu;
        };
        side_slope_.on_result = [this](const int r) {
            bridge_.setNow({{bridge_.params(band_)[BandParams::kSideOrder], static_cast<float>(r - 1)}},
                           "Change Detector Slope");
        };
        bindSideField(side_freq_, BandParams::kSideFreq, "Freq");
        side_freq_.setRange(zlp::PSideFreq::kRange, zlp::PSideFreq::kDefaultV);
        side_freq_.format = [](const float v) { return format::freq(v); };
        side_freq_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = format::parseFreq(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };
        bindSideField(side_q_, BandParams::kSideQ, "Q");
        side_q_.setRange(zlp::PSideQ::kRange, zlp::PSideQ::kDefaultV);
        side_q_.format = [](const float v) { return format::q(v); };
        side_q_.parse = parseGain;
        link_.setTooltip("Detector follows the band's frequency and Q");
        link_.on_click = [this] {
            const auto& bp = bridge_.params(band_);
            bridge_.setNow({{bp[BandParams::kSideLink], bridge_.band(band_).side_link ? 0.f : 1.f}}, "Link Detector");
        };
        swap_.setTooltip("Detect from the opposite stereo channel (e.g. Side for a Mid band)");
        swap_.on_click = [this] {
            const auto& bp = bridge_.params(band_);
            bridge_.setNow({{bp[BandParams::kSideSwap], bridge_.band(band_).side_swap ? 0.f : 1.f}}, "Swap Detector");
        };

        shape_.setLabel("Shape");
        shape_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = band_ < kBandNum ? static_cast<int>(bridge_.band(band_).type) : -1;
            for (int t = 0; t < zlp::PFilterType::kChoices.size(); ++t) {
                menu.addItem(t + 1, band_ops::typeName(static_cast<FilterType>(t)), true, t == current);
            }
            return menu;
        };
        shape_.on_result = [this](const int r) {
            for (const auto b : targets()) {
                band_ops::setType(bridge_, b, static_cast<FilterType>(r - 1));
            }
        };
        shape_.draw_icon = [this](juce::Graphics& g, const juce::Rectangle<float> area, const juce::Colour c) {
            if (band_ < kBandNum) {
                icons::drawShape(g, bridge_.band(band_).type, area, c, 1.4f);
            }
        };

        slope_.setLabel("Slope");
        slope_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = band_ < kBandNum ? static_cast<int>(bridge_.band(band_).order_idx) : -1;
            for (int o = 0; o < zlp::POrder::kChoices.size(); ++o) {
                menu.addItem(o + 1, zlp::POrder::kChoices[o], true, o == current);
            }
            return menu;
        };
        slope_.on_result = [this](const int r) {
            std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
            for (const auto b : targets()) {
                values.emplace_back(bridge_.params(b)[BandParams::kOrder], static_cast<float>(r - 1));
            }
            bridge_.setNow(values, "Change Slope");
        };

        stereo_.setLabel("Stereo");
        stereo_.build_menu = [this] {
            juce::PopupMenu menu;
            const auto current = band_ < kBandNum ? static_cast<int>(bridge_.band(band_).lr) : -1;
            for (int lr = 0; lr < 5; ++lr) {
                menu.addItem(lr + 1, kStereoNames[static_cast<size_t>(lr)], true, lr == current);
            }
            return menu;
        };
        stereo_.on_result = [this](const int r) {
            std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
            for (const auto b : targets()) {
                values.emplace_back(bridge_.params(b)[BandParams::kLR], static_cast<float>(r - 1));
            }
            bridge_.setNow(values, "Change Stereo Placement");
        };

        bindKnob(freq_, Field::kFreq, "Freq");
        freq_.setRange(zlp::PFreq::kRange, zlp::PFreq::kDefaultV);
        freq_.format = [](const float v) { return format::freq(v); };
        freq_.parse = [](const juce::String& t) -> std::optional<float> {
            if (const auto v = format::parseFreq(t)) {
                return static_cast<float>(*v);
            }
            return std::nullopt;
        };

        bindKnob(gain_, Field::kGain, "Gain");
        gain_.setRange(zlp::PGain::kRange, 0.f);
        gain_.setBipolar(true);
        gain_.format = [](const float v) { return format::gain(v); };
        gain_.parse = parseGain;

        bindKnob(q_, Field::kQ, "Q");
        q_.setRange(zlp::PQ::kRange, zlp::PQ::kDefaultV);
        q_.format = [](const float v) { return format::q(v); };
        q_.parse = parseGain;

        bindKnob(range_, Field::kRange, "Range");
        range_.setRange(juce::NormalisableRange<float>(-30.f, 30.f, .01f), 0.f);
        range_.setBipolar(true);
        range_.format = [](const float v) { return format::gain(v); };
        range_.parse = parseGain;

        bindKnob(threshold_, Field::kThreshold, "Threshold");
        threshold_.setRange(zlp::PThreshold::kRange, zlp::PThreshold::kDefaultV);
        threshold_.format = [](const float v) { return format::gain(v, true, false); };
        threshold_.parse = parseGain;

        bindKnob(knee_, Field::kKnee, "Knee");
        knee_.setRange(zlp::PKneeW::kRange, zlp::PKneeW::kDefaultV);
        knee_.format = [](const float v) { return juce::String(v, 1) + " dB"; };
        knee_.parse = parseGain;

        bindKnob(attack_, Field::kAttack, "Attack");
        attack_.setRange(zlp::PAttack::kRange, zlp::PAttack::kDefaultV);
        attack_.format = [](const float v) { return format::ms(v); };
        attack_.parse = parseGain;

        bindKnob(release_, Field::kRelease, "Release");
        release_.setRange(zlp::PRelease::kRange, zlp::PRelease::kDefaultV);
        release_.format = [](const float v) { return format::ms(v); };
        release_.parse = parseGain;

        learn_.setTooltip("Learn threshold and knee from the incoming signal");
        learn_.on_click = [this] {
            if (band_ >= kBandNum) {
                return;
            }
            const auto& b = bridge_.band(band_);
            const auto& bp = bridge_.params(band_);
            if (b.dynamic_learn) {
                // stop learning and keep what was learned
                auto& c = bridge_.getController();
                bridge_.setNow({
                                   {bp[BandParams::kDynamicLearn], 0.f},
                                   {bp[BandParams::kThreshold], static_cast<float>(c.getLearnedThreshold(band_))},
                                   {bp[BandParams::kKnee], static_cast<float>(c.getLearnedKnee(band_))},
                               }, "Learn Threshold");
            } else {
                bridge_.setNow({{bp[BandParams::kDynamicLearn], 1.f}}, "Learn Threshold");
            }
        };
        relative_.setTooltip("Threshold relative to the band's own level");
        relative_.on_click = [this] {
            if (band_ < kBandNum) {
                const auto& bp = bridge_.params(band_);
                bridge_.setNow({{bp[BandParams::kDynamicRelative], bridge_.band(band_).dynamic_relative ? 0.f : 1.f}},
                               "Relative Threshold");
            }
        };

        setInterceptsMouseClicks(true, true);
        setAlpha(0.f);
    }

    BandParams::Field BandPanel::paramField(const Field f) const {
        switch (f) {
        case Field::kFreq: return BandParams::kFreq;
        case Field::kGain: return BandParams::kGain;
        case Field::kQ: return BandParams::kQ;
        case Field::kRange: return BandParams::kTargetGain;
        case Field::kThreshold: return BandParams::kThreshold;
        case Field::kKnee: return BandParams::kKnee;
        case Field::kAttack: return BandParams::kAttack;
        case Field::kRelease: return BandParams::kRelease;
        }
        return BandParams::kFreq;
    }

    std::vector<size_t> BandPanel::targets() const {
        std::vector<size_t> result;
        for (size_t b = 0; b < kBandNum; ++b) {
            if (selection_.isSelected(b) && bridge_.band(b).isActive()) {
                result.push_back(b);
            }
        }
        if (result.empty() && band_ < kBandNum) {
            result.push_back(band_);
        }
        return result;
    }

    void BandPanel::bindKnob(Knob& knob, const Field field, const juce::String& label) {
        knob.setLabel(label);
        knob.on_begin = [this, field] {
            edit_starts_.clear();
            std::vector<juce::RangedAudioParameter*> paras;
            const auto pf = paramField(field);
            for (const auto b : targets()) {
                const auto& s = bridge_.band(b);
                const auto& bp = bridge_.params(b);
                edit_starts_.push_back({b, bp.raw(pf), s.gain, s.target_gain});
                paras.push_back(bp[pf]);
                if (field == Field::kGain && s.dynamic_on) {
                    paras.push_back(bp[BandParams::kTargetGain]);
                }
            }
            bridge_.beginEdit(paras, "Change " + juce::String(bridge_.params(band_)[pf]->getName(32)));
        };
        knob.on_change = [this, field](const float v) {
            if (edit_starts_.empty()) {
                return;
            }
            const auto pf = paramField(field);
            // the lead band takes the value; others follow relatively (ratio for log values, offset for dB)
            const auto lead = std::find_if(edit_starts_.begin(), edit_starts_.end(),
                                           [this](const EditStart& e) { return e.band == band_; });
            const auto& ref = lead != edit_starts_.end() ? *lead : edit_starts_.front();
            for (const auto& e : edit_starts_) {
                const auto& bp = bridge_.params(e.band);
                switch (field) {
                case Field::kFreq:
                case Field::kQ:
                    bridge_.setPlain(bp[pf], e.value * (v / std::max(ref.value, 1e-4f)));
                    break;
                case Field::kGain: {
                    const auto gain = std::clamp(e.value + (v - ref.value), -30.f, 30.f);
                    bridge_.setPlain(bp[pf], gain);
                    if (bridge_.band(e.band).dynamic_on) {
                        bridge_.setPlain(bp[BandParams::kTargetGain],
                                         std::clamp(e.target + (gain - e.gain), -30.f, 30.f));
                    }
                    break;
                }
                case Field::kRange:
                    // range is shown relative to the static gain
                    bridge_.setPlain(bp[pf], std::clamp(e.gain + v, -30.f, 30.f));
                    break;
                case Field::kThreshold:
                case Field::kKnee:
                case Field::kAttack:
                case Field::kRelease:
                    bridge_.setPlain(bp[pf], e.value + (v - ref.value));
                    break;
                }
            }
        };
        knob.on_end = [this] {
            bridge_.endEdit();
            edit_starts_.clear();
        };
    }

    void BandPanel::bindSideField(ValueField& field, const BandParams::Field f, const juce::String& label) {
        field.setLabel(label);
        field.on_begin = [this, f] {
            const auto& bp = bridge_.params(band_);
            juce::RangedAudioParameter* paras[] = {bp[f], bp[BandParams::kSideLink]};
            bridge_.beginEdit(paras, "Change Detector");
            bridge_.setPlain(bp[BandParams::kSideLink], 0.f);
        };
        field.on_change = [this, f](const float v) { bridge_.setPlain(bridge_.params(band_)[f], v); };
        field.on_end = [this] { bridge_.endEdit(); };
    }

    void BandPanel::step(const bool forwards) {
        std::vector<size_t> order;
        for (size_t b = 0; b < kBandNum; ++b) {
            if (bridge_.band(b).isActive()) {
                order.push_back(b);
            }
        }
        if (order.empty()) {
            return;
        }
        std::ranges::sort(order, [this](const size_t a, const size_t b) {
            return bridge_.band(a).freq < bridge_.band(b).freq;
        });
        const auto it = std::ranges::find(order, band_);
        const auto idx = it == order.end() ? 0 : static_cast<size_t>(std::distance(order.begin(), it));
        selection_.select(order[forwards ? (idx + 1) % order.size() : (idx + order.size() - 1) % order.size()]);
    }

    bool BandPanel::tick() {
        const auto primary = selection_.getPrimary();
        const auto show = primary < kBandNum && bridge_.band(primary).isActive();
        if (!show) {
            if (solo_latched_ && on_solo) {
                solo_latched_ = false;
                on_solo(kBandNum, false);
            }
            return false;
        }
        const auto layout_changed = primary != band_ || selection_.count() != count_;
        if (layout_changed || selection_.getVersion() != seen_version_ || bridge_.getChangedBands()[primary]
            || bridge_.haveGlobalsChanged()) {
            const auto was_dynamic = band_ < kBandNum && bridge_.band(band_).dynamic_on;
            if (primary != band_ && solo_latched_ && on_solo) {
                solo_latched_ = false;
                on_solo(kBandNum, false);
            }
            band_ = primary;
            count_ = selection_.count();
            seen_version_ = selection_.getVersion();
            sync();
            if (layout_changed || was_dynamic != bridge_.band(band_).dynamic_on) {
                resized();
            }
            repaint();
        }
        if (get_solo) {
            const auto side = get_solo_side && get_solo_side();
            solo_.setOn(get_solo() == band_ && !side);
            side_solo_.setOn(get_solo() == band_ && side);
        }
        if (bridge_.band(band_).dynamic_learn) {
            // show the learned values live while learning
            auto& c = bridge_.getController();
            threshold_.setValue(static_cast<float>(c.getLearnedThreshold(band_)));
            knee_.setValue(static_cast<float>(c.getLearnedKnee(band_)));
        }
        return true;
    }

    void BandPanel::sync() {
        const auto& b = bridge_.band(band_);
        const auto c = colour::band(band_);
        for (auto* k : {&freq_, &gain_, &q_, &range_, &threshold_, &knee_, &attack_, &release_}) {
            k->setAccent(c);
        }
        shape_.setAccent(c);
        shape_.setText(band_ops::typeName(b.type));
        slope_.setText(zlp::POrder::kChoices[static_cast<int>(b.order_idx)]);
        slope_.setEnabled(b.hasSlope());
        slope_.setAlpha(b.hasSlope() ? 1.f : .4f);
        stereo_.setText(kStereoNames[static_cast<size_t>(b.lr)]);

        freq_.setValue(b.freq);
        gain_.setValue(b.gain);
        gain_.setEnabledLook(b.hasGain());
        q_.setValue(b.q);
        q_.setEnabledLook(b.hasQ());

        dynamic_.setOn(b.dynamic_on);
        dynamic_.setOnColour(c);
        bypass_.setOn(b.status == zlp::kBypass);
        bypass_.setOnColour(colour::kWarning);
        solo_.setOnColour(c);

        const auto dyn = b.dynamic_on;
        for (auto* comp : std::initializer_list<juce::Component*>{
                 &range_, &threshold_, &knee_, &attack_, &release_, &learn_, &relative_,
                 &side_type_, &side_slope_, &side_freq_, &side_q_, &link_, &swap_, &side_solo_
             }) {
            comp->setVisible(dyn);
        }
        if (dyn) {
            range_.setValue(b.target_gain - b.gain);
            range_.setEnabledLook(b.hasGain());
            threshold_.setValue(b.threshold);
            knee_.setValue(b.knee);
            attack_.setValue(b.attack);
            release_.setValue(b.release);
            learn_.setOn(b.dynamic_learn);
            learn_.setOnColour(c);
            relative_.setOn(b.dynamic_relative);
            relative_.setOnColour(c);
            side_type_.setText(kSideTypeNames[std::min<size_t>(b.side_type, 3)]);
            side_slope_.setText(zlp::PSideOrder::kChoices[static_cast<int>(b.side_order_idx)]);
            const auto filtered = b.side_type != 3;
            side_slope_.setEnabled(filtered);
            side_slope_.setAlpha(filtered ? 1.f : .4f);
            side_freq_.setValue(b.side_freq);
            side_freq_.setEnabledLook(filtered);
            side_q_.setValue(b.side_q);
            side_q_.setEnabledLook(b.side_type == 0);
            link_.setOn(b.side_link);
            link_.setOnColour(c);
            swap_.setOn(b.side_swap);
            swap_.setOnColour(c);
            side_solo_.setOnColour(c);
        }
        setTitle("Band " + juce::String(band_ + 1));
    }

    int BandPanel::getIdealHeight() const {
        const auto dyn = band_ < kBandNum && bridge_.band(band_).dynamic_on;
        return static_cast<int>(kHeaderH + kBodyH + (dyn ? kDynamicH + kSideH : 0.f));
    }

    int BandPanel::getIdealWidth() const {
        return 548;
    }

    void BandPanel::resized() {
        auto b = getLocalBounds().toFloat().reduced(10.f, 0.f);
        auto header = b.removeFromTop(kHeaderH).reduced(0.f, 4.f);
        header.removeFromLeft(78.f); // band label, painted
        prev_.setBounds(header.removeFromLeft(22.f).toNearestInt());
        next_.setBounds(header.removeFromLeft(22.f).toNearestInt());
        delete_.setBounds(header.removeFromRight(24.f).toNearestInt());
        header.removeFromRight(4.f);
        bypass_.setBounds(header.removeFromRight(24.f).toNearestInt());
        header.removeFromRight(4.f);
        solo_.setBounds(header.removeFromRight(24.f).toNearestInt());
        header.removeFromRight(4.f);
        dynamic_.setBounds(header.removeFromRight(24.f).toNearestInt());

        auto body = b.removeFromTop(kBodyH).reduced(0.f, 6.f);
        auto selectors = body.removeFromLeft(200.f).reduced(0.f, 8.f);
        shape_.setBounds(selectors.removeFromTop(26.f).toNearestInt());
        selectors.removeFromTop(8.f);
        auto row = selectors.removeFromTop(26.f);
        slope_.setBounds(row.removeFromLeft(row.getWidth() * .5f - 4.f).toNearestInt());
        row.removeFromLeft(8.f);
        stereo_.setBounds(row.toNearestInt());

        body.removeFromLeft(20.f);
        const auto knob_gap = (body.getWidth() - 3.f * kKnobW) / 2.f;
        freq_.setBounds(body.removeFromLeft(kKnobW).toNearestInt());
        body.removeFromLeft(knob_gap);
        gain_.setBounds(body.removeFromLeft(kKnobW).toNearestInt());
        body.removeFromLeft(knob_gap);
        q_.setBounds(body.removeFromLeft(kKnobW).toNearestInt());

        if (band_ < kBandNum && bridge_.band(band_).dynamic_on) {
            auto dyn = b.removeFromTop(kDynamicH).reduced(0.f, 4.f);
            auto chips = dyn.removeFromLeft(200.f).reduced(0.f, 22.f);
            learn_.setBounds(chips.removeFromLeft(chips.getWidth() * .5f - 4.f).toNearestInt());
            chips.removeFromLeft(8.f);
            relative_.setBounds(chips.toNearestInt());
            dyn.removeFromLeft(4.f);
            const auto w = dyn.getWidth() / 5.f;
            for (auto* k : {&range_, &threshold_, &knee_, &attack_, &release_}) {
                k->setBounds(dyn.removeFromLeft(w).withSizeKeepingCentre(std::min(w, kKnobW), dyn.getHeight())
                                .toNearestInt());
            }
            auto side = b.removeFromTop(kSideH).reduced(0.f, 8.f);
            side.removeFromLeft(78.f); // caption, painted
            side_type_.setBounds(side.removeFromLeft(74.f).toNearestInt());
            side.removeFromLeft(6.f);
            side_slope_.setBounds(side.removeFromLeft(96.f).toNearestInt());
            side.removeFromLeft(6.f);
            side_freq_.setBounds(side.removeFromLeft(100.f).toNearestInt());
            side_q_.setBounds(side.removeFromLeft(62.f).toNearestInt());
            side_solo_.setBounds(side.removeFromRight(24.f).toNearestInt());
            side.removeFromRight(4.f);
            swap_.setBounds(side.removeFromRight(static_cast<float>(swap_.getIdealWidth())).toNearestInt());
            side.removeFromRight(2.f);
            link_.setBounds(side.removeFromRight(static_cast<float>(link_.getIdealWidth())).toNearestInt());
        }
    }

    void BandPanel::paint(juce::Graphics& g) {
        const auto bounds = getLocalBounds().toFloat();
        // soft shadow
        for (int i = 3; i > 0; --i) {
            g.setColour(juce::Colours::black.withAlpha(.07f));
            g.fillRoundedRectangle(bounds.reduced(static_cast<float>(3 - i)).translated(0.f, static_cast<float>(i)),
                                   size::kRadiusLarge);
        }
        g.setColour(colour::kRaised.withAlpha(.97f));
        g.fillRoundedRectangle(bounds.reduced(2.f), size::kRadiusLarge);
        g.setColour(colour::kHairlineStrong);
        g.drawRoundedRectangle(bounds.reduced(2.5f), size::kRadiusLarge, 1.f);

        if (band_ >= kBandNum) {
            return;
        }
        const auto c = colour::band(band_);
        auto header = bounds.reduced(12.f, 0.f).removeFromTop(kHeaderH);
        auto dot = header.removeFromLeft(12.f);
        g.setColour(c);
        g.fillEllipse(juce::Rectangle<float>(8.f, 8.f).withCentre(dot.getCentre()));
        header.removeFromLeft(4.f);
        g.setColour(colour::kTextPrimary);
        g.setFont(Fonts::get().font(type::kBody, Fonts::kSemiBold));
        const auto title = count_ > 1 ? juce::String(count_) + " bands" : "Band " + juce::String(band_ + 1);
        g.drawText(title, header.removeFromLeft(64.f), juce::Justification::centredLeft);

        // divider under the header
        g.setColour(colour::kHairline);
        g.fillRect(juce::Rectangle<float>(bounds.getX() + 12.f, kHeaderH, bounds.getWidth() - 24.f, 1.f));
        if (bridge_.band(band_).dynamic_on) {
            g.fillRect(juce::Rectangle<float>(bounds.getX() + 12.f, kHeaderH + kBodyH, bounds.getWidth() - 24.f, 1.f));
            g.setColour(colour::kTextTertiary);
            g.setFont(Fonts::get().font(9.5f, Fonts::kSemiBold).withExtraKerningFactor(.08f));
            g.drawText("DYNAMICS", juce::Rectangle<float>(12.f, kHeaderH + kBodyH + 10.f, 120.f, 12.f),
                       juce::Justification::centredLeft);
            const auto side_y = kHeaderH + kBodyH + kDynamicH;
            g.setColour(colour::kHairline);
            g.fillRect(juce::Rectangle<float>(bounds.getX() + 12.f, side_y, bounds.getWidth() - 24.f, 1.f));
            g.setColour(colour::kTextTertiary);
            g.drawText("DETECTOR", juce::Rectangle<float>(12.f, side_y, 80.f, kSideH), juce::Justification::centredLeft);
        }
    }
}
