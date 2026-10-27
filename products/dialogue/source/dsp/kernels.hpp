// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <algorithm>

namespace altvane::dialogue::dsp::kernels {
    /**
     * c[m][n] = a[m][k] · b[k][n] + bias[n] (bias may be null), or c += a · b if accumulate; row-major and dense.
     * Large products go to the platform BLAS (Accelerate on Apple, whose sgemm runs on the matrix units),
     * small ones to a register-tiled loop that the compiler vectorises
     */
    void gemm(int m, int n, int k, const float* a, const float* b, const float* bias, float* c, bool accumulate = false);

    /**
     * on by default; off sends every product to the portable loop (tests compare the two, benchmarks time them)
     */
    void setBlasEnabled(bool enabled);

    /**
     * tanh to within 4e-7, from a clamped rational approximation that vectorises (no branches, no libm calls)
     */
    inline float tanh(float x) {
        constexpr float kClamp = 7.90531110763549805f;
        x = std::clamp(x, -kClamp, kClamp);
        const float x2 = x * x;
        float p = x2 * -2.76076847742355e-16f + 2.00018790482477e-13f;
        p = x2 * p + -8.60467152213735e-11f;
        p = x2 * p + 5.12229709037114e-08f;
        p = x2 * p + 1.48572235717979e-05f;
        p = x2 * p + 6.37261928875436e-04f;
        p = x2 * p + 4.89352455891786e-03f;
        float q = x2 * 1.19825839466702e-06f + 1.18534705686654e-04f;
        q = x2 * q + 2.26843463243900e-03f;
        q = x2 * q + 4.89352518554385e-03f;
        return x * p / q;
    }

    inline float sigmoid(const float x) {
        return .5f + .5f * tanh(.5f * x);
    }

    inline void tanh(float* x, const int n) {
        for (int i = 0; i < n; ++i) {
            x[i] = tanh(x[i]);
        }
    }

    inline void sigmoid(float* x, const int n) {
        for (int i = 0; i < n; ++i) {
            x[i] = sigmoid(x[i]);
        }
    }

    inline void relu(float* x, const int n) {
        for (int i = 0; i < n; ++i) {
            x[i] = std::max(x[i], 0.f);
        }
    }
}
