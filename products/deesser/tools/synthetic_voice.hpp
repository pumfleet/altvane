// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <juce_core/juce_core.h>

#include "dsp/filters.hpp"

/**
 * a synthetic voice for the tools: a phrase of vowels (a glottal-ish harmonic stack shaped by three formants),
 * esses at varied levels and frequencies and a plosive, over a quiet room, looping every 2.5 seconds
 */
class SyntheticVoice {
public:
    explicit SyntheticVoice(const double sample_rate) : rate_(sample_rate) {
    }

    float next() {
        const auto t = std::fmod(time_, kLoop);
        time_ += 1.0 / rate_;
        float y = .0015f * (random_.nextFloat() * 2.f - 1.f);
        for (const auto& e : kEvents) {
            if (t < e.start || t >= e.start + e.length) {
                continue;
            }
            const auto u = (t - e.start) / e.length;
            // rise and fall: quick for esses, smooth for vowels
            const auto edge = e.kind == Kind::kVowel ? .12 : .25;
            const auto env = static_cast<float>(std::min({1.0, u / edge, (1.0 - u) / edge}));
            if (e.kind == Kind::kVowel) {
                y += e.level * env * vowel(e.f0 + (e.f1 - e.f0) * u);
            } else {
                y += e.level * env * noise(e.kind, e.f0);
            }
        }
        return y;
    }

private:
    enum class Kind { kVowel, kEss, kPlosive };

    struct Event {
        Kind kind;
        double start, length;
        float level;
        double f0, f1;
    };

    static constexpr double kLoop = 2.5;
    static constexpr std::array<Event, 10> kEvents{{
        {Kind::kEss, .00, .13, .20f, 7200.0, 0.0},
        {Kind::kVowel, .12, .30, .34f, 185.0, 205.0},
        {Kind::kEss, .46, .10, .11f, 6500.0, 0.0},
        {Kind::kVowel, .55, .40, .40f, 205.0, 170.0},
        {Kind::kPlosive, 1.00, .018, .30f, 2500.0, 0.0},
        {Kind::kVowel, 1.02, .28, .30f, 175.0, 190.0},
        {Kind::kEss, 1.36, .19, .30f, 7600.0, 0.0},
        {Kind::kVowel, 1.58, .46, .38f, 190.0, 160.0},
        {Kind::kEss, 2.10, .12, .17f, 6800.0, 0.0},
        {Kind::kVowel, 2.24, .20, .26f, 170.0, 175.0},
    }};

    double rate_, time_{0.0}, phase_{0.0};
    juce::Random random_{2024};
    altvane::deesser::dsp::SVF ess1_, ess2_;

    float vowel(const double f0) {
        phase_ += f0 / rate_;
        phase_ -= std::floor(phase_);
        double v = 0.0;
        for (int h = 1; h * f0 < std::min(12000.0, .45 * rate_); ++h) {
            const auto hz = h * f0;
            // three formants of an open vowel, on a falling source
            auto formant = [hz](const double centre, const double width, const double gain) {
                const auto d = std::log2(hz / centre) / width;
                return gain * std::exp(-d * d);
            };
            const auto shape = .08 + formant(700.0, .35, 1.0) + formant(1200.0, .3, .6) + formant(2600.0, .25, .25);
            v += std::sin(2.0 * juce::MathConstants<double>::pi * phase_ * h) * shape / std::pow(h, .8);
        }
        return static_cast<float>(.6 * v);
    }

    float noise(const Kind kind, const double centre) {
        const auto white = random_.nextFloat() * 2.f - 1.f;
        if (kind == Kind::kPlosive) {
            return white;
        }
        const auto c = altvane::deesser::dsp::SVFCoeffs::make(centre, 1.1, rate_);
        return 2.2f * c.k * ess2_.process(c.k * ess1_.process(white, c).bp, c).bp;
    }
};
