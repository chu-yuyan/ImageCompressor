# 图像压缩 – 项目说明

## 编译环境
- Windows + Visual Studio / MinGW   
- 项目包含：`main.cpp`, `PicReader.h/cpp`, `Huffman.h`, `DCT.h`, `rle.h`

## 命令行用法

```bash
# 有损压缩 / 解压 (DCT+量化+哈夫曼)
your_project.exe -compress lena.tiff             # 输出 lena.dat
your_project.exe -read lena.dat                  # 解压并显示
# 无损压缩 / 解压
your_project.exe -compress_lossless lena.tiff    # 输出 lena.dat
your_project.exe -read_lossless lena.dat         # 解压并显示
```
## 压缩率（相对于读出来的字节数）
| 方案 | 压缩率 |
| --- | --- |
| 只用哈夫曼 | 83% |
| 竖着读 + RLE + 哈夫曼 | 88% |
| 差分 + RLE + 哈夫曼 | 64% |
| 差分 + RLE（仅Alpha通道）+ 哈夫曼 | 49%（最终采用）🔴对.tiff文件为65%|
| DCT + RLE + 哈夫曼（有损尝试（质量50）） | 20% |
| DCT + Zigzag + RLE + 哈夫曼 | 20% |
| DCT + Zigzag + DPCM + RLE + 哈夫曼 | 9% |
| DCT + Zigzag + DPCM + RLE（Alpha固定255）+ 哈夫曼 | 9%（最终采用）🔴对.tiff文件为12.1% |
