#include "PicReader.h"
#include"Huffman.h"
#include <stdio.h>
#include<iostream>
using namespace std;

void compress(const char* name)
{
	PicReader imread;
	BYTE *data = nullptr;
	UINT x, y;
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
	cout << "读像素" << endl;
	

	int freq[256]{};
	for (size_t i = 0; i < (x * y * 4);i++) 
	{
		freq[data[i]]++;
	}
	std::vector<uint32_t> codes;
	std::vector<uint8_t> codeLens;
	HuffNode* huffRoot = buildHuffmanTree(freq, codes, codeLens);

	out.write(reinterpret_cast<const char*>(&x), sizeof(x));
	out.write(reinterpret_cast<const char*>(&y), sizeof(y));

	for (int i = 0; i < 256; ++i) {
		uint8_t len = codeLens[i];
		out.write(reinterpret_cast<const char*>(&len), 1);
	}

	// 写入压缩数据
	BitWriter bw(out);
	for (size_t i = 0; i < (x * y * 4); ++i) {
		uint8_t byte = data[i];
		bw.writeBits(codes[byte], codeLens[byte]);
	}
	bw.flush();

	out.close();
	delete[] data;
	data = nullptr;
	deleteTree(huffRoot);

	ifstream check(namedat, ios::binary | ios::ate);
	streamsize compressedSize = check.tellg();
	check.close();
	cout << "Original size: " << (x * y * 4) << " bytes" << endl;
	cout << "Compressed size: " << compressedSize << " bytes" << endl;
	cout << "Compression ratio: " << (double)compressedSize / (x * y * 4) << endl;
}

void read(const char* name)
{
	ifstream in(name, ios::in | ios::binary);
	if (!in)
	{
		cerr << "Cannot open file" << endl;
		return;
	}
	UINT x, y;
	in.read(reinterpret_cast<char*>(&x), sizeof(x));
	in.read(reinterpret_cast<char*>(&y), sizeof(y));
	uint8_t codeLens[256];
	in.read(reinterpret_cast<char*>(codeLens), 256);
	
	struct DecodeNode 
	{
		int child[2];
		int symbol; 
		DecodeNode() : child{ -1, -1 }, symbol(-1) {}
	};
	std::vector<DecodeNode> decodeTree(1);

	std::vector<uint8_t> symbolsWithLen[17]; // 索引为码长
	for (int i = 0; i < 256; ++i) {
		if (codeLens[i] > 0) {
			symbolsWithLen[codeLens[i]].push_back((uint8_t)i);
		}
	}
	uint32_t code = 0;
	for (int len = 1; len <= 16; ++len) {
		for (uint8_t sym : symbolsWithLen[len]) {
			// 将 (code, len) 插入解码树
			uint32_t tmp = code;
			int nodeIdx = 0;
			for (int bitPos = len - 1; bitPos >= 0; --bitPos) { // 高位先走
				int bit = (tmp >> bitPos) & 1;
				if (decodeTree[nodeIdx].child[bit] == -1) {
					decodeTree[nodeIdx].child[bit] = decodeTree.size();
					decodeTree.emplace_back();
				}
				nodeIdx = decodeTree[nodeIdx].child[bit];
			}
			decodeTree[nodeIdx].symbol = sym;
			code++;
		}
		code <<= 1;
	}

	// 读取压缩数据并解码
	size_t totalBytes = (size_t)x * y * 4;
	BYTE* data = new BYTE[totalBytes];
	BitReader br(in);
	size_t written = 0;
	int nodeIdx = 0;
	while (written < totalBytes) {
		bool bit = br.readBit();
		nodeIdx = decodeTree[nodeIdx].child[bit ? 1 : 0];
		if (decodeTree[nodeIdx].symbol != -1) {
			data[written++] = (BYTE)decodeTree[nodeIdx].symbol;
			nodeIdx = 0; // 回到根节点
		}
	}

	in.close();
	// 显示图片
	PicReader imread;
	std::cout << "显示图片" << std::endl;
	imread.showPic(data, x, y);
	delete[] data;
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
		std::cout << "Compressing " << argv[2] << " ..." << std::endl;
		compress(argv[2]);
	}
	else if (strcmp(argv[1], "-read") == 0) 
	{
		std::cout << "Reading " << argv[2] << " ..." << std::endl;
		read(argv[2]);
	}
	else 
	{
		std::cerr << "Invalid command. Use -compress or -read." << std::endl;
		return 1;
	}

	return 0;
}