#include "PicReader.h"
#include "Huffman.h"
#include "rle.h"
#include "DCT.h"
#include <stdio.h>
#include <iostream>
#include <fstream>  
#include <cstring>
#include <array>
#include<algorithm>
using namespace std;

void writeU32(ofstream& out, uint32_t v) 
{ 
    out.write(reinterpret_cast<const char*>(&v), sizeof(v)); 
}
bool readU32(ifstream& in, uint32_t& v) 
{ 
    return (bool)in.read(reinterpret_cast<char*>(&v), sizeof(v)); 
}

void compress(const char* name)
{
    PicReader imread;
    BYTE* data = nullptr;
    UINT x = 0, y = 0;
    imread.readPic(name);
    imread.getData(data, x, y);
    

    string namedat = name;
    size_t dot = namedat.find_last_of('.');
    if (dot != string::npos) namedat = namedat.substr(0, dot);
    namedat += ".dat";
    ofstream out(namedat, ios::out | ios::binary);
    if (!out)
    {
        cerr << "Cannot create file" << endl;
        delete[] data;
        return;
    }

    //列优先
    const vector<uint8_t> planar = rgbaToPlanar(data, x, y);//uint8_t无符号整数刚好 8 位,取值范围：0 ~ 255

    //差分
    const vector<uint8_t> diff = diffEncode(planar, x, y);
    const vector<uint8_t> rleData = rleEncode(diff);

    // Huffman 
    uint32_t freq[256]{};
    for (uint8_t b : rleData) freq[b]++;

    vector<uint32_t> treeCodes;
    vector<uint8_t> codeLens;
    HuffNode* root = buildHuffmanTree(freq, treeCodes, codeLens);

    // Canonical codes
    vector<uint32_t> canonCodes;
    if (!buildCanonicalCodes(codeLens, canonCodes))
    {
        cerr << "Failed to build canonical codes." << endl;
        delete[] data;
        deleteTree(root);
        return;
    }

    // magic：是压缩的
    const uint32_t magic = 0x3252335Au; // 'Z3R2'
    writeU32(out, magic);
    writeU32(out, (uint32_t)x);
    writeU32(out, (uint32_t)y);
    writeU32(out, (uint32_t)rleData.size());

    //写入 256 个 codeLens（256B）
    out.write(reinterpret_cast<const char*>(codeLens.data()), (streamsize)codeLens.size());

    // 写入
    BitWriter bw(out);
    for (uint8_t b : rleData)
    {
        bw.writeBits(canonCodes[b], codeLens[b]);
    }
    bw.flush();

    out.close();
    delete[] data;
    deleteTree(root);

    ifstream check(namedat, ios::binary | ios::ate);
    streamsize compressedSize = check.tellg();
    check.close();

    const size_t originalBytes = (size_t)x * y * 4;
    cout << "Output file: " << namedat << endl;
    cout << "Original size: " << originalBytes << " bytes" << endl;
    cout << "RLE size: " << rleData.size() << " bytes" << endl;
    cout << "Compressed size: " << compressedSize << " bytes" << endl;
    cout << "Compression ratio: " << (double)compressedSize / (double)originalBytes << endl;
}

void read(const char* name)
{
    ifstream in(name, ios::in | ios::binary);
    if (!in)
    {
        cerr << "Cannot open file" << endl;
        return;
    }

    uint32_t magic = 0, x32 = 0, y32 = 0, rleSize = 0;

    if (!readU32(in, magic) || magic != 0x3252335Au)
    {
        cerr << "Bad file magic." << endl;
        return;
    }
    if (!readU32(in, x32) || !readU32(in, y32) || !readU32(in, rleSize))
    {
        cerr << "Bad header." << endl;
        return;
    }

    // 读 codeLens
    vector<uint8_t> codeLens(256, 0);
    if (!in.read(reinterpret_cast<char*>(codeLens.data()), (streamsize)codeLens.size()))
    {
        cerr << "Bad header." << endl;
        return;
    }
    //建树
    vector<uint32_t> canonCodes;
    if (!buildCanonicalCodes(codeLens, canonCodes))
    {
        cerr << "Bad code lengths (cannot build canonical codes)." << endl;
        return;
    }

    HuffNode* root = buildDecodeTreeFromCanonical(canonCodes, codeLens);
    if (!root)
    {
        cerr << "Failed to build decode tree." << endl;
        return;
    }

    vector<uint8_t> rleData((size_t)rleSize);
    BitReader br(in);
    if (!decodeBytesWithHuffmanTree(br, root, (BYTE*)rleData.data(), rleData.size()))
    {
        cerr << "Decode failed (EOF / corrupt stream)." << endl;
        deleteTree(root);
        return;
    }
    deleteTree(root);

    // RLE -> diff
    vector<uint8_t> diff;
    const size_t expectedPlanar = (size_t)x32 * y32 * 4;
    if (!rleDecode(rleData, diff, expectedPlanar,(UINT)x32, (UINT)y32))
    {
        cerr << "RLE decode failed." << endl;
        return;
    }

    // diff -> planar
    diffDecodeInplace(diff, x32 , y32);

    // planar -> RGBA
    vector<BYTE> rgba = planarToRgba(diff, (UINT)x32, (UINT)y32);

    PicReader imread;
    imread.showPic((const BYTE*)rgba.data(), (UINT)x32, (UINT)y32);
}

void Lcompress(const char* name)
{
    PicReader imread;
    BYTE* data = nullptr;
    UINT x = 0, y = 0;
    imread.readPic(name);
    imread.getData(data, x, y);

    string namedat = name;
    size_t dot = namedat.find_last_of('.');
    if (dot != string::npos) namedat = namedat.substr(0, dot);
    namedat += ".dat";
    ofstream out(namedat, ios::out | ios::binary);
    if (!out)
    {
        cerr << "Cannot create file" << endl;
        delete[] data;
        return;
    }

    //分开读
    const vector<uint8_t> planar = rgbaToPlanar(data, x, y);
    const size_t pixels = (size_t)x * y;
    const uint8_t* chans[3] = {planar.data() + pixels * 0, planar.data() + pixels * 1, planar.data() + pixels * 2};

    //分块8*8
    const uint32_t quality = 50;
    const UINT blocksX = (x + 7) / 8;
    const UINT blocksY = (y + 7) / 8;
    const size_t blocks = (size_t)blocksX * blocksY;

	//每个块的DC差分/AC的run-level
    vector<uint8_t> payload;
    payload.reserve(blocks * 6 * 4);

    
    int16_t prevDC[3] = {0,0,0};

    uint8_t block[64];
    for (int c = 0; c < 3; ++c)
    {
        for (UINT by = 0; by < blocksY; ++by)
        {
            for (UINT bx = 0; bx < blocksX; ++bx)
            {
                for (int yy = 0; yy < 8; yy++)
                {
                    const UINT py = (by * 8 + (UINT)yy < y) ? (by * 8 + (UINT)yy) : (y - 1);
                    for (int xx = 0; xx < 8; xx++)
                    {
                        const UINT px = (bx * 8 + (UINT)xx < x) ? (bx * 8 + (UINT)xx) : (x - 1);
                        block[yy * 8 + xx] = chans[c][(size_t)py * x + px];
                    }
                }
				// DCT -> quant -> zigzag
                auto F = forward8x8(block);
                auto Q = quantize(F, (int)quality);
                auto zig = zigzagScan(Q);

                // DC
                int16_t dc = zig[0];
                int16_t diff = dc - prevDC[c];
                prevDC[c] = dc;
                payload.push_back((uint8_t)(diff & 0xFF));
                payload.push_back((uint8_t)((diff >> 8) & 0xFF));

                // AC
                auto rl = encodeBlockAC(zig);
                for (const auto& e : rl)
                {
                    payload.push_back(e.run);
                    payload.push_back((uint8_t)(e.level & 0xFF));
                    payload.push_back((uint8_t)((e.level >> 8) & 0xFF));
                }
            }
        }
    }

    // Huffman encode payload
    uint32_t freq[256]{};
    for (uint8_t b : payload) freq[b]++;

    vector<uint32_t> treeCodes;
    vector<uint8_t> codeLens;
    HuffNode* root = buildHuffmanTree(freq, treeCodes, codeLens);

    vector<uint32_t> canonCodes;
    if (!buildCanonicalCodes(codeLens, canonCodes))
    {
        cerr << "Failed to build canonical codes." << endl;
        delete[] data;
        deleteTree(root);
        return;
    }

    // magic + x + y + quality + payloadBytes + payloadBytes (second field reused) + 256 code lengths
    const uint32_t magic = 0x5443445Au; // 'ZDCT'
    writeU32(out, magic);
    writeU32(out, (uint32_t)x);
    writeU32(out, (uint32_t)y);
    writeU32(out, quality);
    writeU32(out, (uint32_t)payload.size());
    writeU32(out, (uint32_t)payload.size());

    out.write(reinterpret_cast<const char*>(codeLens.data()), (streamsize)codeLens.size());

    BitWriter bw(out);
    for (uint8_t b : payload)
        bw.writeBits(canonCodes[b], codeLens[b]);
    bw.flush();

    out.close();
    delete[] data;
    deleteTree(root);

    ifstream check(namedat, ios::binary | ios::ate);
    streamsize compressedSize = check.tellg();
    check.close();

    const size_t originalBytes = (size_t)x * y * 4;
    cout << "Output file: " << namedat << endl;
    cout << "Original size: " << originalBytes << " bytes" << endl;
    cout << "Payload size: " << payload.size() << " bytes" << endl;
    cout << "Compressed size: " << compressedSize << " bytes" << endl;
    cout << "Compression ratio: " << (double)compressedSize / (double)originalBytes << endl;
}

void Lread(const char* name)
{
    ifstream in(name, ios::in | ios::binary);
    if (!in)
    {
        cerr << "Cannot open file" << endl;
        return;
    }

    uint32_t magic = 0, x32 = 0, y32 = 0, quality = 0, payloadSize32 = 0, payloadSize32b = 0;
    if (!readU32(in, magic) || magic != 0x5443445Au)
    {
        cerr << "Bad file magic." << endl;
        return;
    }
    if (!readU32(in, x32) || !readU32(in, y32) || !readU32(in, quality) || !readU32(in, payloadSize32) || !readU32(in, payloadSize32b))
    {
        cerr << "Bad header." << endl;
        return;
    }

    vector<uint8_t> codeLens(256, 0);
    if (!in.read(reinterpret_cast<char*>(codeLens.data()), (streamsize)codeLens.size()))
    {
        cerr << "Bad header." << endl;
        return;
    }

    vector<uint32_t> canonCodes;
    if (!buildCanonicalCodes(codeLens, canonCodes))
    {
        cerr << "Bad code lengths (cannot build canonical codes)." << endl;
        return;
    }

    HuffNode* root = buildDecodeTreeFromCanonical(canonCodes, codeLens);
    if (!root)
    {
        cerr << "Failed to build decode tree." << endl;
        return;
    }

    const size_t payloadSize = (size_t)payloadSize32;
    vector<uint8_t> payload(payloadSize);
    BitReader br(in);
    if (!decodeBytesWithHuffmanTree(br, root, (BYTE*)payload.data(), payload.size()))
    {
        cerr << "Decode failed (EOF / corrupt stream)." << endl;
        deleteTree(root);
        return;
    }
    deleteTree(root);

    // Parse payload into blocks
    const UINT x = (UINT)x32;
    const UINT y = (UINT)y32;
    const size_t pixels = (size_t)x * y;
    //alpha set to 255
    vector<uint8_t> planar(pixels * 4, 255);
    uint8_t* chans[3] = {
        planar.data() + pixels * 0,
        planar.data() + pixels * 1,
        planar.data() + pixels * 2
    };

    const UINT blocksX = (x + 7) / 8;
    const UINT blocksY = (y + 7) / 8;

    size_t payloadPos = 0;
    int16_t prevDC[3] = {0,0,0};
    uint8_t outBlock[64];
    
	// 逐通道逐块解码
    for (int c = 0; c < 3; ++c)
    {
        for (UINT by = 0; by < blocksY; ++by)
        {
            for (UINT bx = 0; bx < blocksX; ++bx)
            {
                if (payloadPos + 2 > payload.size()) { cerr << "Payload truncated (DC)." << endl; return; }
                int16_t dc_diff = (int16_t)((int16_t)payload[payloadPos] | ((int16_t)payload[payloadPos+1] << 8));
                payloadPos += 2;
                int16_t dc = prevDC[c] + dc_diff;
                prevDC[c] = dc;

                std::array<int16_t,64> zig{};
                zig[0] = dc;
                int idx = 1;
                while (idx < 64)
                {
                    if (payloadPos + 3 > payload.size()) { cerr << "Payload truncated (AC)." << endl; return; }
                    uint8_t run = payload[payloadPos++];
                    int16_t level = (int16_t)((int16_t)payload[payloadPos] | ((int16_t)payload[payloadPos+1] << 8));
                    payloadPos += 2;
                    // EOB
                    if (run == 0 && level == 0)
                    {
                        break;
                    }
                    if (run == 255 && level == 0)
                    {
                        int toSkip = min(255, 64 - idx);
                        idx += toSkip;
                        continue;
                    }
                    idx += run;
                    if (idx >= 64) { cerr << "Run overflow." << endl; return; }
                    zig[(size_t)idx] = level;
                    idx++;
                }

                auto Q = inverseZigzagScan(zig);
                auto F = dequantize(Q, (int)quality);
                inverse8x8(F, outBlock);

                for (int yy = 0; yy < 8; ++yy)
                {
                    const UINT py = by * 8 + (UINT)yy;
                    if (py >= y) break;
                    for (int xx = 0; xx < 8; ++xx)
                    {
                        const UINT px = bx * 8 + (UINT)xx;
                        if (px >= x) break;
                        chans[c][(size_t)py * x + px] = outBlock[yy * 8 + xx];
                    }
                }
            }
        }
    }

    vector<BYTE> rgba = planarToRgba(planar, x, y);
    PicReader imread;
    imread.showPic((const BYTE*)rgba.data(), x, y);
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " -compress <input>  or  -read <compressed>" << std::endl;
        return 1;
    }

    if (strcmp(argv[1], "-compress_lossless") == 0)
    {
        cout << "Compressing " << argv[2] << "..." << endl;
        compress(argv[2]);
    }
    else if (strcmp(argv[1], "-read_lossless") == 0)
    {
        cout << "Reading " << argv[2] << "..." << endl;
        read(argv[2]);
    }
    else if (strcmp(argv[1], "-compress") == 0)
    {
        cout << "Compressing" << argv[2] << "..." << endl;
        Lcompress(argv[2]);
    }
    else if (strcmp(argv[1], "-read") == 0)
    {
        cout << "Reading " << argv[2] << "..." << endl;
        Lread(argv[2]);
    }
    else
    {
        std::cerr << "Invalid command. Use -compress or -read." << std::endl;
        return 1;
    }

    return 0;
}