#pragma once
#include "PicReader.h"
#include <vector>
#include <cstdint>
#include <stdexcept>

// RGBA -> planar
inline std::vector<uint8_t> rgbaToPlanar(const BYTE* data, UINT x, UINT y)
{
    const size_t pixels = (size_t)x * y;
    std::vector<uint8_t> out(pixels * 4);

    uint8_t* R = out.data() + pixels * 0;
    uint8_t* G = out.data() + pixels * 1;
    uint8_t* B = out.data() + pixels * 2;
    uint8_t* A = out.data() + pixels * 3;

    for (size_t i = 0; i < pixels; ++i)
    {
        R[i] = data[i * 4 + 0];
        G[i] = data[i * 4 + 1];
        B[i] = data[i * 4 + 2];
        A[i] = data[i * 4 + 3];
    }
    return out;
}

// planar -> RGBA
inline std::vector<BYTE> planarToRgba(const std::vector<uint8_t>& planar, UINT x, UINT y)
{
    const size_t pixels = (size_t)x * y;
    if (planar.size() != pixels * 4)
        throw std::runtime_error("planarToRgba: bad planar size");

    std::vector<BYTE> out(pixels * 4);

    const uint8_t* R = planar.data() + pixels * 0;
    const uint8_t* G = planar.data() + pixels * 1;
    const uint8_t* B = planar.data() + pixels * 2;
    const uint8_t* A = planar.data() + pixels * 3;

    for (size_t i = 0; i < pixels; ++i)
    {
        out[i * 4 + 0] = R[i];
        out[i * 4 + 1] = G[i];
        out[i * 4 + 2] = B[i];
        out[i * 4 + 3] = A[i];
    }

    return out;
}

// ===== 差分编码/解码（按字节模256）=====
// out[i] = in[i] - in[i-1] (mod 256), out[0]=in[0]
inline std::vector<uint8_t> diffEncode(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> out;
    out.resize(input.size());
    if (input.empty()) return out;

    out[0] = input[0];
    for (size_t i = 1; i < input.size(); ++i)
        out[i] = (uint8_t)((uint8_t)input[i] - (uint8_t)input[i - 1]);

    return out;
}

// in-place 反差分：x[i] = x[i] + x[i-1] (mod 256)
inline void diffDecodeInplace(std::vector<uint8_t>& data)
{
    for (size_t i = 1; i < data.size(); ++i)
        data[i] = (uint8_t)((uint8_t)data[i] + (uint8_t)data[i - 1]);
}

// ===== RLE: [value,count]... count=1..255 =====
inline std::vector<uint8_t> rleEncode(const std::vector<uint8_t>& input)
{
    std::vector<uint8_t> out;
    out.reserve(input.size());

    const size_t n = input.size();
    size_t i = 0;
    while (i < n)
    {
        const uint8_t v = input[i];
        uint8_t count = 1;

        while (i + count < n && input[i + count] == v && count < 255)
            ++count;

        out.push_back(v);
        out.push_back(count);
        i += count;
    }
    return out;
}

inline bool rleDecode(const std::vector<uint8_t>& rle, std::vector<uint8_t>& output, size_t expectedSize)
{
    output.clear();
    output.reserve(expectedSize);

    if ((rle.size() % 2) != 0)
        return false;

    for (size_t i = 0; i < rle.size(); i += 2)
    {
        const uint8_t v = rle[i];
        const uint8_t c = rle[i + 1];
        if (c == 0) return false;

        if (output.size() + c > expectedSize)
            return false;

        output.insert(output.end(), (size_t)c, v);
    }

    return output.size() == expectedSize;
}