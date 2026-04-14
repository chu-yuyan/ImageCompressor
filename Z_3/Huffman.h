#pragma once
#include <queue>
#include <vector>
#include <fstream>
#include <cstdint>
#include <algorithm>
using namespace std;

class BitWriter
{
private:
    ofstream& out;
    uint8_t buffer;
    int bits_in_buffer;

public:
    BitWriter(ofstream& os) : out(os), buffer(0), bits_in_buffer(0) {}

    void writeBit(bool bit)
    {
        buffer = (uint8_t)((buffer << 1) | (bit ? 1 : 0));
        bits_in_buffer++;
        if (bits_in_buffer == 8)
        {
            out.write(reinterpret_cast<char*>(&buffer), 1);
            buffer = 0;
            bits_in_buffer = 0;
        }
    }

    void writeBits(uint32_t bits, int len)
    {
        for (int i = len - 1; i >= 0; --i)
            writeBit(((bits >> i) & 1U) != 0);
    }

    void flush()
    {
        if (bits_in_buffer > 0)
        {
            buffer = (uint8_t)(buffer << (8 - bits_in_buffer));
            out.write(reinterpret_cast<char*>(&buffer), 1);
            buffer = 0;
            bits_in_buffer = 0;
        }
    }
};

class BitReader
{
private:
    ifstream& in;
    uint8_t buffer;
    int bits_in_buffer;
    bool ok;

public:
    BitReader(ifstream& is) : in(is), buffer(0), bits_in_buffer(0), ok(true) {}

    bool good() const { return ok; }

    bool readBit()
    {
        if (!ok) return false;

        if (bits_in_buffer == 0)
        {
            if (!in.read(reinterpret_cast<char*>(&buffer), 1))
            {
                ok = false;
                return false;
            }
            bits_in_buffer = 8;
        }

        const bool bit = ((buffer >> (bits_in_buffer - 1)) & 1U) != 0;
        bits_in_buffer--;
        return bit;
    }
};

struct HuffNode
{
    uint8_t byte;    
    uint32_t freq;
    uint32_t order; 
    HuffNode* left;
    HuffNode* right;

    HuffNode(uint8_t b, uint32_t f, uint32_t o) : byte(b), freq(f), order(o), left(nullptr), right(nullptr) {}
    HuffNode(uint32_t f, uint32_t o, HuffNode* l, HuffNode* r) : byte(0), freq(f), order(o), left(l), right(r) {}

    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

struct CompareNode
{
    bool operator()(const HuffNode* a, const HuffNode* b) const
    {
        if (a->freq != b->freq) return a->freq > b->freq;      
        return a->order > b->order; 
    }
};

inline void deleteTree(HuffNode* root)
{
    if (!root) return;
    deleteTree(root->left);
    deleteTree(root->right);
    delete root;
}

inline void generateCodes(HuffNode* root, uint32_t code, int len,
    vector<uint32_t>& codes,vector<uint8_t>& codeLens)
{
    if (!root) return;

    if (root->isLeaf())
    {
        codes[root->byte] = code;
        codeLens[root->byte] = (uint8_t)len;
        return;
    }

    if (root->left)  generateCodes(root->left, code << 1, len + 1, codes, codeLens);
    if (root->right) generateCodes(root->right, (code << 1) | 1U, len + 1, codes, codeLens);
}

inline HuffNode* buildHuffmanTree(const uint32_t freq[256],
    vector<uint32_t>& codes, vector<uint8_t>& codeLens)
{
    priority_queue<HuffNode*, vector<HuffNode*>, CompareNode> pq;

    uint32_t nextOrder = 0;
    for (int i = 0; i < 256; ++i)
    {
        if (freq[i] > 0)
            pq.push(new HuffNode((uint8_t)i, freq[i], nextOrder++));
    }

    if (pq.empty())
    {
        codes.assign(256, 0);
        codeLens.assign(256, 0);
        return nullptr;
    }

    if (pq.size() == 1)
    {
        // 只有一个符号时，给它加一个父节点，避免码长为0
        HuffNode* leaf = pq.top(); pq.pop();
        HuffNode* parent = new HuffNode(leaf->freq, nextOrder++, leaf, nullptr);
        pq.push(parent);
    }

    while (pq.size() > 1)
    {
        HuffNode* a = pq.top(); pq.pop();
        HuffNode* b = pq.top(); pq.pop();

        HuffNode* parent = new HuffNode(a->freq + b->freq, nextOrder++, a, b);
        pq.push(parent);
    }

    HuffNode* root = pq.top();

    codes.assign(256, 0);
    codeLens.assign(256, 0);
    generateCodes(root, 0, 0, codes, codeLens);
    return root;
}

// Canonical Huffman helpers
bool buildCanonicalCodes(const vector<uint8_t>& codeLens, vector<uint32_t>& codes)
{
    if (codeLens.size() != 256) return false;

    codes.assign(256, 0);

    int maxLen = 0;
    for (int i = 0; i < 256; i++) maxLen = max(maxLen, (int)codeLens[i]);
    if (maxLen == 0) return false;

    vector<uint32_t> blCount((size_t)maxLen + 1, 0);
    for (int i = 0; i < 256; ++i)
    {
        const int len = (int)codeLens[i];
        if (len < 0) return false;
        if (len > 0) blCount[(size_t)len]++;
    }

    vector<uint32_t> nextCode((size_t)maxLen + 1, 0);
    uint32_t code = 0;
    for (int bits = 1; bits <= maxLen; ++bits)
    {
        code = (code + blCount[(size_t)bits - 1]) << 1;
        nextCode[(size_t)bits] = code;
    }

    // 排序规则：先按码长升序，再按符号值升序
    for (int sym = 0; sym < 256; ++sym)
    {
        const int len = (int)codeLens[sym];
        if (len == 0) continue;
        codes[sym] = nextCode[(size_t)len]++;
    }
    return true;
}

inline HuffNode* buildDecodeTreeFromCanonical(const vector<uint32_t>& codes, const vector<uint8_t>& codeLens)
{
    if (codes.size() != 256 || codeLens.size() != 256) return nullptr;

    HuffNode* root = new HuffNode(0u, 0u, nullptr, nullptr);

    for (int sym = 0; sym < 256; ++sym)
    {
        const int len = (int)codeLens[sym];
        if (len == 0) continue;

        HuffNode* cur = root;
        const uint32_t code = codes[sym];

        for (int i = len - 1; i >= 0; --i)
        {
            const bool bit = ((code >> i) & 1U) != 0;
            HuffNode*& next = bit ? cur->right : cur->left;
            if (!next)
            {
                next = new HuffNode(0u, 0u, nullptr, nullptr);
            }
            cur = next;
        }

        cur->byte = (uint8_t)sym;
        // leaf 判定用的是左右为空；如果你希望更严格可额外校验“没有重复插入”
        cur->left = nullptr;
        cur->right = nullptr;
    }

    return root;
}

inline bool decodeBytesWithHuffmanTree(BitReader& br, HuffNode* root, BYTE* outData, size_t totalBytes)
{
    if (!root) return false;

    size_t written = 0;

    while (written < totalBytes)
    {
        HuffNode* cur = root;

        while (cur && !cur->isLeaf())
        {
            const bool bit = br.readBit();
            if (!br.good()) return false;
            cur = bit ? cur->right : cur->left;
        }

        if (!cur) return false;
        outData[written++] = (BYTE)cur->byte;
    }

    return true;
}