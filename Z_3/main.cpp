#include "PicReader.h"
#include "Huffman.h"
#include "rle.h"
#include <stdio.h>
#include <iostream>
#include <fstream>  
#include <cstring>
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
    const vector<uint8_t> diff = diffEncode(planar);
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

    // canonical rebuild
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
    if (!rleDecode(rleData, diff, expectedPlanar))
    {
        cerr << "RLE decode failed." << endl;
        return;
    }

    // diff -> planar
    diffDecodeInplace(diff);

    // planar -> RGBA
    vector<BYTE> rgba = planarToRgba(diff, (UINT)x32, (UINT)y32);

    PicReader imread;
    imread.showPic((const BYTE*)rgba.data(), (UINT)x32, (UINT)y32);
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " -compress <input>  or  -read <compressed>" << std::endl;
        return 1;
    }

    if (strcmp(argv[1], "-compress") == 0)
    {
        cout << "Compressing " << argv[2] << "..." << endl;
        compress(argv[2]);
    }
    else if (strcmp(argv[1], "-read") == 0)
    {
        cout << "Reading " << argv[2] << "..." << endl;
        read(argv[2]);
    }
    else
    {
        std::cerr << "Invalid command. Use -compress or -read." << std::endl;
        return 1;
    }

    return 0;
}