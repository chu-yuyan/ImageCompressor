#pragma once

#include <cstdint>
#include <array>
#include <cmath>

// Minimal 8x8 DCT/IDCT + quantization helpers (header-only, C++14)
namespace dct_impl
{
    constexpr int BS = 8;
    constexpr double kPi = 3.141592653589793238462643383279502884;

    inline double c(int u) { return (u == 0) ? (1.0 / std::sqrt(2.0)) : 1.0; }

    inline int clampi(int v, int lo, int hi)
    {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    inline const std::array<uint8_t, 64>& defaultQ()
    {
        static const std::array<uint8_t, 64> q = { {
            16,11,10,16,24,40,51,61,
            12,12,14,19,26,58,60,55,
            14,13,16,24,40,57,69,56,
            14,17,22,29,51,87,80,62,
            18,22,37,56,68,109,103,77,
            24,35,55,64,81,104,113,92,
            49,64,78,87,103,121,120,101,
            72,92,95,98,112,100,103,99
        } };
        return q;
    }

    inline std::array<double, 64> forward8x8(const uint8_t* block)
    {
        std::array<double, 64> F{};
        for (int u = 0; u < BS; ++u)
        {
            for (int v = 0; v < BS; ++v)
            {
                double sum = 0.0;
                for (int x = 0; x < BS; ++x)
                {
                    for (int y = 0; y < BS; ++y)
                    {
                        const double fxy = double(block[x * BS + y]) - 128.0;
                        sum += fxy * std::cos(((2 * x + 1) * u * kPi) / 16.0) *
                                     std::cos(((2 * y + 1) * v * kPi) / 16.0);
                    }
                }
                F[u * BS + v] = 0.25 * c(u) * c(v) * sum;
            }
        }
        return F;
    }

    inline void inverse8x8(const std::array<double, 64>& F, uint8_t* outBlock)
    {
        for (int x = 0; x < BS; ++x)
        {
            for (int y = 0; y < BS; ++y)
            {
                double sum = 0.0;
                for (int u = 0; u < BS; ++u)
                {
                    for (int v = 0; v < BS; ++v)
                    {
                        sum += c(u) * c(v) * F[u * BS + v] *
                               std::cos(((2 * x + 1) * u * kPi) / 16.0) *
                               std::cos(((2 * y + 1) * v * kPi) / 16.0);
                    }
                }
                int val = (int)std::lround(0.25 * sum + 128.0);
                outBlock[x * BS + y] = (uint8_t)clampi(val, 0, 255);
            }
        }
    }

    inline std::array<int16_t, 64> quantize(const std::array<double, 64>& F, int quality)
    {
        quality = clampi(quality, 1, 100);
        int scale;
        if (quality < 50) scale = 5000 / quality;
        else scale = 200 - quality * 2;

        std::array<int16_t, 64> Q{};
        const auto& base = defaultQ();
        for (int i = 0; i < 64; ++i)
        {
            int q = (base[i] * scale + 50) / 100;
            q = clampi(q, 1, 255);
            Q[i] = (int16_t)std::lround(F[i] / double(q));
        }
        return Q;
    }

    inline std::array<double, 64> dequantize(const std::array<int16_t, 64>& Q, int quality)
    {
        quality = clampi(quality, 1, 100);
        int scale;
        if (quality < 50) scale = 5000 / quality;
        else scale = 200 - quality * 2;

        std::array<double, 64> F{};
        const auto& base = defaultQ();
        for (int i = 0; i < 64; ++i)
        {
            int q = (base[i] * scale + 50) / 100;
            q = clampi(q, 1, 255);
            F[i] = double(Q[i]) * double(q);
        }
        return F;
    }
}

// Expose as `dct::...`
namespace dct = ::dct_impl;
