#pragma once
#include <queue>
#include <vector>
#include <bitset>
#include<fstream>

class BitWriter 
{
private:
    std::ofstream& out;
    uint8_t buffer;      
    int bits_in_buffer;   
public:
    BitWriter(std::ofstream& os) : out(os), buffer(0), bits_in_buffer(0) {}

    void writeBit(bool bit) 
    {
        buffer = (buffer << 1) | (bit ? 1 : 0);
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
        {
            writeBit((bits >> i) & 1);
        }
    }

    void flush() 
    {
        if (bits_in_buffer > 0) 
        {
            buffer <<= (8 - bits_in_buffer);
            out.write(reinterpret_cast<char*>(&buffer), 1);
            buffer = 0;
            bits_in_buffer = 0;
        }
    }
};

class BitReader {
private:
    std::ifstream& in;
    uint8_t buffer;
    int bits_in_buffer;
public:
    BitReader(std::ifstream& is) : in(is), buffer(0), bits_in_buffer(0) {}

    bool readBit() 
    {
        if (bits_in_buffer == 0) {
            if (!in.read(reinterpret_cast<char*>(&buffer), 1)) 
            {
                return false;
            }
            bits_in_buffer = 8;
        }
        bool bit = (buffer >> (bits_in_buffer - 1)) & 1;
        bits_in_buffer--;
        return bit;
    }

    uint32_t readBits(int len) 
    {
        uint32_t val = 0;
        for (int i = len - 1; i >= 0; --i) 
        {
            val = (val << 1) | (readBit() ? 1 : 0);
        }
        return val;
    }
};

struct HuffNode {
    uint8_t byte;          
    int freq;            
    HuffNode* left;
    HuffNode* right;
    HuffNode(uint8_t b, int f) : byte(b), freq(f), left(nullptr), right(nullptr) {}
    HuffNode(int f, HuffNode* l, HuffNode* r) : byte(0), freq(f), left(l), right(r) {}
};

struct CompareNode 
{
    bool operator()(HuffNode* a, HuffNode* b) 
    {
        return a->freq > b->freq;
    }
};

void deleteTree(HuffNode* root) 
{
    if (!root) return;
    deleteTree(root->left);
    deleteTree(root->right);
    delete root;
}

void generateCodes(HuffNode* root, uint32_t code, int len, std::vector<uint32_t>& codes, std::vector<uint8_t>& codeLens) 
{
    if (!root->left && !root->right) 
    {
        codes[root->byte] = code;
        codeLens[root->byte] = len;
        return;
    }
    if (root->left) generateCodes(root->left, code << 1, len + 1, codes, codeLens);
    if (root->right) generateCodes(root->right, (code << 1) | 1, len + 1, codes, codeLens);
}

HuffNode* buildHuffmanTree(const int freq[256], std::vector<uint32_t>& codes, std::vector<uint8_t>& codeLens) {
    std::priority_queue<HuffNode*, std::vector<HuffNode*>, CompareNode> pq;
    for (int i = 0; i < 256; ++i) {
        if (freq[i] > 0) {
            pq.push(new HuffNode((uint8_t)i, freq[i]));
        }
    }
    if (pq.size() == 1) {
        HuffNode* leaf = pq.top(); pq.pop();
        HuffNode* parent = new HuffNode(leaf->freq, leaf, nullptr);
        pq.push(parent);
    }
    while (pq.size() > 1) {
        HuffNode* a = pq.top(); pq.pop();
        HuffNode* b = pq.top(); pq.pop();
        HuffNode* parent = new HuffNode(a->freq + b->freq, a, b);
        pq.push(parent);
    }
    HuffNode* root = pq.top();
    codes.resize(256, 0);
    codeLens.resize(256, 0);
    generateCodes(root, 0, 0, codes, codeLens);
    return root;
}