#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>

// This header provides:
//  - lz77Encode / lz77Decode
//  - packAndMaybeLZ77 / unpackAndMaybeLZ77
// Main is restricted to calling compress/read and can pass x/y.

inline void writeU32LE(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}
inline bool readU32LE(const std::vector<uint8_t>& in, size_t& pos, uint32_t& v)
{
    if (pos + 4 > in.size()) return false;
    v = (uint32_t)in[pos] |
        ((uint32_t)in[pos + 1] << 8) |
        ((uint32_t)in[pos + 2] << 16) |
        ((uint32_t)in[pos + 3] << 24);
    pos += 4;
    return true;
}

// ---------------- LZ77 core ----------------
// Token format:
//  - Literal:  [0][literal_byte]
//  - Match:    [1][offset_lo][offset_hi][len]
// offset: 1..WINDOW, len: 3..255

inline std::vector<uint8_t> lz77Encode(const std::vector<uint8_t>& in, size_t windowSize = 4096, size_t maxLen = 255)
{
    std::vector<uint8_t> out;
    out.reserve(in.size());

    const size_t n = in.size();
    size_t i = 0;

    while (i < n)
    {
        const size_t winStart = (i > windowSize) ? (i - windowSize) : 0;

        size_t bestLen = 0;
        size_t bestOff = 0;

        // Naive match finder
        for (size_t j = i; j-- > winStart;)
        {
            size_t k = 0;
            while (k < maxLen && i + k < n && in[j + k] == in[i + k])
            {
                ++k;
                if (j + k >= i) break;
            }

            if (k > bestLen)
            {
                bestLen = k;
                bestOff = i - j;
                if (bestLen == maxLen) break;
            }
        }

        if (bestLen >= 3 && bestOff >= 1 && bestOff <= 65535)
        {
            out.push_back(1);
            out.push_back((uint8_t)(bestOff & 0xFF));
            out.push_back((uint8_t)((bestOff >> 8) & 0xFF));
            out.push_back((uint8_t)bestLen);
            i += bestLen;
        }
        else
        {
            out.push_back(0);
            out.push_back(in[i]);
            ++i;
        }
    }

    return out;
}

inline bool lz77Decode(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
    out.clear();

    size_t i = 0;
    while (i < in.size())
    {
        const uint8_t tag = in[i++];
        if (tag == 0)
        {
            if (i >= in.size()) return false;
            out.push_back(in[i++]);
        }
        else if (tag == 1)
        {
            if (i + 2 >= in.size()) return false;
            const uint16_t off = (uint16_t)in[i] | ((uint16_t)in[i + 1] << 8);
            const uint8_t len = in[i + 2];
            i += 3;

            if (off == 0 || len < 3) return false;
            if (off > out.size()) return false;

            const size_t start = out.size() - off;
            for (uint8_t k = 0; k < len; ++k)
                out.push_back(out[start + k]);
        }
        else
        {
            return false;
        }
    }

    return true;
}

// ---------------- Pipeline helpers ----------------
// We want after diff (planar x*y*4):
//   RLE on each channel separately, then LZ77 on RGB streams only, then concatenate.
// For decoding, we need the 4 stream sizes to split.
// We store a small sub-header at the start of the returned byte stream:
//   'LZ3' (4 bytes) + sizeR + sizeG + sizeB + sizeA  (each uint32 LE)

#include "rle.h" // uses rleEncode/rleDecode

inline std::vector<uint8_t> packAndMaybeLZ77(const std::vector<uint8_t>& diffPlanar, UINT x, UINT y)
{
    const size_t pixels = (size_t)x * y;

    std::vector<uint8_t> diffR(diffPlanar.begin() + 0 * pixels, diffPlanar.begin() + 1 * pixels);
    std::vector<uint8_t> diffG(diffPlanar.begin() + 1 * pixels, diffPlanar.begin() + 2 * pixels);
    std::vector<uint8_t> diffB(diffPlanar.begin() + 2 * pixels, diffPlanar.begin() + 3 * pixels);
    std::vector<uint8_t> diffA(diffPlanar.begin() + 3 * pixels, diffPlanar.begin() + 4 * pixels);

    std::vector<uint8_t> rleR = rleEncode(diffR);
    std::vector<uint8_t> rleG = rleEncode(diffG);
    std::vector<uint8_t> rleB = rleEncode(diffB);
    std::vector<uint8_t> rleA = rleEncode(diffA);

    std::vector<uint8_t> lzR = lz77Encode(rleR);
    std::vector<uint8_t> lzG = lz77Encode(rleG);
    std::vector<uint8_t> lzB = lz77Encode(rleB);

    std::vector<uint8_t> out;
    out.reserve(4 + 16 + lzR.size() + lzG.size() + lzB.size() + rleA.size());

    // magic 'LZ3'
    out.push_back('L'); out.push_back('Z'); out.push_back('3'); out.push_back(' ');

    writeU32LE(out, (uint32_t)lzR.size());
    writeU32LE(out, (uint32_t)lzG.size());
    writeU32LE(out, (uint32_t)lzB.size());
    writeU32LE(out, (uint32_t)rleA.size());

    out.insert(out.end(), lzR.begin(), lzR.end());
    out.insert(out.end(), lzG.begin(), lzG.end());
    out.insert(out.end(), lzB.begin(), lzB.end());
    out.insert(out.end(), rleA.begin(), rleA.end());

    return out;
}

inline bool unpackAndMaybeLZ77(const std::vector<uint8_t>& packed, std::vector<uint8_t>& outDiffPlanar, UINT x, UINT y, size_t expectedPlanarSize)
{
    outDiffPlanar.clear();

    const size_t pixels = (size_t)x * y;
    if (expectedPlanarSize != pixels * 4) return false;

    if (packed.size() < 4 + 16) return false;
    if (!(packed[0] == 'L' && packed[1] == 'Z' && packed[2] == '3')) return false;

    size_t pos = 4;
    uint32_t szR = 0, szG = 0, szB = 0, szA = 0;
    if (!readU32LE(packed, pos, szR) || !readU32LE(packed, pos, szG) || !readU32LE(packed, pos, szB) || !readU32LE(packed, pos, szA))
        return false;

    const size_t need = (size_t)szR + szG + szB + szA;
    if (pos + need != packed.size()) return false;

    const size_t offR = pos;
    const size_t offG = offR + szR;
    const size_t offB = offG + szG;
    const size_t offA = offB + szB;

    std::vector<uint8_t> lzR(packed.begin() + offR, packed.begin() + offG);
    std::vector<uint8_t> lzG(packed.begin() + offG, packed.begin() + offB);
    std::vector<uint8_t> lzB(packed.begin() + offB, packed.begin() + offA);
    std::vector<uint8_t> rleA(packed.begin() + offA, packed.end());

    std::vector<uint8_t> rleR, rleG, rleB;
    if (!lz77Decode(lzR, rleR) || !lz77Decode(lzG, rleG) || !lz77Decode(lzB, rleB))
        return false;

    std::vector<uint8_t> diffR, diffG, diffB, diffA;
    if (!rleDecode(rleR, diffR, pixels, x, y) || !rleDecode(rleG, diffG, pixels, x, y) || !rleDecode(rleB, diffB, pixels, x, y) || !rleDecode(rleA, diffA, pixels, x, y))
        return false;

    outDiffPlanar.reserve(expectedPlanarSize);
    outDiffPlanar.insert(outDiffPlanar.end(), diffR.begin(), diffR.end());
    outDiffPlanar.insert(outDiffPlanar.end(), diffG.begin(), diffG.end());
    outDiffPlanar.insert(outDiffPlanar.end(), diffB.begin(), diffB.end());
    outDiffPlanar.insert(outDiffPlanar.end(), diffA.begin(), diffA.end());

    return outDiffPlanar.size() == expectedPlanarSize;
}
