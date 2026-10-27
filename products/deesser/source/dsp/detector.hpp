// Copyright (C) 2026 - Altvane
// This file is part of Altvane De-Esser
//
// Altvane De-Esser is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane De-Esser is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane De-Esser. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include "filters.hpp"

namespace altvane::deesser::dsp {
    enum class Mode { kVocal, kAllround };

    /**
     * band level over body level (dB) to sibilance confidence: none at -12 dB, all at 0 dB and above
     */
    inline float sibilanceConfidence(const float band_over_body_db) {
        const auto t = std::clamp((band_over_body_db + 12.f) / 12.f, 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    /**
     * what the detector heard in one sample, linear (decibels are taken only when needed: see Engine)
     */
    struct Detection {
        float band{0.f};  // the detection band signal
        float level{0.f}; // its peak envelope, what the threshold sees
        // Vocal mode: the band and the body below it with the same ballistics (both 1 in Allround)
        float band_slow{1.f}, body{1.f};

        /**
         * 0..1: how much of the overshoot counts
         */
        float sibilance() const { return sibilanceConfidence(gainToDb(band_slow / std::max(body, 1e-9f))); }

        /**
         * sibilance() > .5, without the logarithm
         */
        bool isSibilant() const { return band_slow > .501187f * body; }
    };

    /**
     * sibilance detection for one channel: the side chain through the detection band (a fourth-order high-pass at
     * the band's low edge, and a low-pass at its high edge unless open), followed by a peak envelope.
     * in Vocal mode the band is weighed against the body of the signal below it: an ess is as loud as (or louder
     * than) the voice under it, while a vowel's own high harmonics sit 20 to 30 dB below its body
     */
    class Detector {
    public:
        void prepare(const double sample_rate) {
            band_attack_ = onePole(sample_rate, .0003);
            band_release_ = onePole(sample_rate, .030);
            body_attack_ = onePole(sample_rate, .005);
            body_release_ = onePole(sample_rate, .120);
            reset();
        }

        void reset() {
            for (auto* f : {&hp1_, &hp2_, &lp1_, &lp2_, &body_}) {
                f->reset();
            }
            band_env_ = 0.f;
            band_slow_env_ = 0.f;
            body_env_ = 0.f;
        }

        struct Filters {
            SVFCoeffs hp1, hp2, lp1, lp2, body;
            bool open{true};
        };

        Detection process(const float x, const Filters& f, const Mode mode) {
            auto band = hp2_.process(hp1_.process(x, f.hp1).hp, f.hp2).hp;
            if (!f.open) {
                band = lp2_.process(lp1_.process(band, f.lp1).lp, f.lp2).lp;
            } else {
                // idle while open, so closing the band starts it from silence, not from what it held back then
                lp1_.reset();
                lp2_.reset();
            }
            follow(band_env_, std::abs(band), band_attack_, band_release_);
            Detection d;
            d.band = band;
            d.level = band_env_;
            if (mode == Mode::kVocal) {
                // band and body compared with the same ballistics, so a sharp onset (a click, a plosive's burst)
                // does not look like an ess just because the fast envelope got there first
                follow(band_slow_env_, std::abs(band), body_attack_, body_release_);
                follow(body_env_, std::abs(body_.process(x, f.body).lp), body_attack_, body_release_);
                d.band_slow = band_slow_env_;
                d.body = body_env_;
            } else {
                // rests in Allround, so Vocal starts afresh rather than from levels heard before the switch
                body_.reset();
                band_slow_env_ = 0.f;
                body_env_ = 0.f;
            }
            return d;
        }

    private:
        SVF hp1_, hp2_, lp1_, lp2_, body_;
        float band_env_{0.f}, band_slow_env_{0.f}, body_env_{0.f};
        float band_attack_{1.f}, band_release_{1.f}, body_attack_{1.f}, body_release_{1.f};

        static void follow(float& env, const float x, const float attack, const float release) {
            env += (x - env) * (x > env ? attack : release);
        }
    };
}
