// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include "kernels.hpp"

#include <atomic>
#include <cstring>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#define ALTVANE_DIALOGUE_BLAS 1
#elif defined(ALTVANE_DIALOGUE_CBLAS)
#include <cblas.h>
#define ALTVANE_DIALOGUE_BLAS 1
#endif

namespace altvane::dialogue::dsp::kernels {
    namespace {
        std::atomic<bool> blas_enabled{true};

#if ALTVANE_DIALOGUE_BLAS
        /**
         * whether a product is worth a BLAS call: many rows sharing one weight matrix, or one large matrix;
         * never the small per-step recurrent products, where the call would cost more than the arithmetic
         */
        bool useBlas(const int m, const int n, const int k) {
            constexpr long kMinWork = 1 << 16;
            const auto weights = static_cast<long>(n) * k;
            return blas_enabled.load(std::memory_order::relaxed)
                   && ((m >= 16 && weights * m >= kMinWork) || weights >= kMinWork);
        }
#endif

        /**
         * kRows rows of c += a · b, 64 accumulators per tile so they stay in registers
         */
        template <int kRows>
        void tile(const int n, const int k, const float* a, const float* b, float* c) {
            constexpr int kCols = 64 / kRows;
            int j = 0;
            for (; j + kCols <= n; j += kCols) {
                float acc[kRows][kCols];
                for (int r = 0; r < kRows; ++r) {
                    for (int col = 0; col < kCols; ++col) {
                        acc[r][col] = c[r * n + j + col];
                    }
                }
                for (int p = 0; p < k; ++p) {
                    const float* row = b + p * n + j;
                    for (int r = 0; r < kRows; ++r) {
                        const float v = a[r * k + p];
                        for (int col = 0; col < kCols; ++col) {
                            acc[r][col] += v * row[col];
                        }
                    }
                }
                for (int r = 0; r < kRows; ++r) {
                    for (int col = 0; col < kCols; ++col) {
                        c[r * n + j + col] = acc[r][col];
                    }
                }
            }
            if (j == n) {
                return;
            }
            // the last, narrower tile
            const int cols = n - j;
            float acc[kRows][kCols];
            for (int r = 0; r < kRows; ++r) {
                for (int col = 0; col < cols; ++col) {
                    acc[r][col] = c[r * n + j + col];
                }
            }
            for (int p = 0; p < k; ++p) {
                const float* row = b + p * n + j;
                for (int r = 0; r < kRows; ++r) {
                    const float v = a[r * k + p];
                    for (int col = 0; col < cols; ++col) {
                        acc[r][col] += v * row[col];
                    }
                }
            }
            for (int r = 0; r < kRows; ++r) {
                for (int col = 0; col < cols; ++col) {
                    c[r * n + j + col] = acc[r][col];
                }
            }
        }
    }

    void gemm(const int m, const int n, const int k, const float* a, const float* b, const float* bias, float* c,
              const bool accumulate) {
        if (!accumulate) {
            for (int i = 0; i < m; ++i) {
                if (bias != nullptr) {
                    std::memcpy(c + i * n, bias, static_cast<size_t>(n) * sizeof(float));
                } else {
                    std::memset(c + i * n, 0, static_cast<size_t>(n) * sizeof(float));
                }
            }
        } else if (bias != nullptr) {
            for (int i = 0; i < m; ++i) {
                for (int j = 0; j < n; ++j) {
                    c[i * n + j] += bias[j];
                }
            }
        }
#if ALTVANE_DIALOGUE_BLAS
        if (useBlas(m, n, k)) {
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, m, n, k, 1.f, a, k, b, n, 1.f, c, n);
            return;
        }
#endif
        int i = 0;
        for (; i + 4 <= m; i += 4) {
            tile<4>(n, k, a + i * k, b, c + i * n);
        }
        for (; i + 2 <= m; i += 2) {
            tile<2>(n, k, a + i * k, b, c + i * n);
        }
        for (; i < m; ++i) {
            tile<1>(n, k, a + i * k, b, c + i * n);
        }
    }

    void setBlasEnabled(const bool enabled) {
        blas_enabled.store(enabled);
    }
}
