// main/yz_glyph.h —— 拓本字形包解码（纯 C，主机可测）。
//
// 字形包格式（tools/gen_yz_assets.py 生成，小端）：
//   头      "YZG1" | u16 字数 | u16 边长 | u32 数据区偏移
//   索引表  每字 u32 偏移（相对数据区）| u32 字节数
//   数据区  每字一段 4 bpp 游程编码：
//           0x00–0x7F  (b+1) 个 0 像素
//           0x80–0xFF  (b&0x7F)+1 个字面像素，随后每字节两个 4 bit，高位在前
// 解码结果是 A8（每像素一字节透明度，0 = 石面，255 = 笔画最亮处）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define YZ_GLYPH_SIZE 176
#define YZ_GLYPH_PIXELS (YZ_GLYPH_SIZE * YZ_GLYPH_SIZE)

typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t count;
    uint32_t data_offset;
} yz_glyph_pack_t;

// 校验头部与索引表（每段都必须落在包内）。失败返回 false，pack 不可用。
bool yz_glyph_pack_open(yz_glyph_pack_t *pack, const uint8_t *data, size_t size);

// 把第 index 个字形解码到 dst（YZ_GLYPH_PIXELS 字节）。数据损坏或越界时把 dst 清零并返回 false。
bool yz_glyph_decode(const yz_glyph_pack_t *pack, int index, uint8_t *dst);
