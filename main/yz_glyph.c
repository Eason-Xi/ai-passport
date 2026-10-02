// main/yz_glyph.c —— 拓本字形包解码，格式见 yz_glyph.h。
#include "yz_glyph.h"

#include <string.h>

#define HEADER_SIZE 12u
#define RECORD_SIZE 8u

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool yz_glyph_pack_open(yz_glyph_pack_t *pack, const uint8_t *data, size_t size) {
    memset(pack, 0, sizeof(*pack));
    if (!data || size < HEADER_SIZE || memcmp(data, "YZG1", 4) != 0) return false;
    const uint16_t count = rd_u16(data + 4);
    const uint16_t edge = rd_u16(data + 6);
    const uint32_t data_offset = rd_u32(data + 8);
    if (edge != YZ_GLYPH_SIZE || count == 0) return false;
    if ((size_t)HEADER_SIZE + (size_t)count * RECORD_SIZE > data_offset || data_offset > size) return false;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *rec = data + HEADER_SIZE + (size_t)i * RECORD_SIZE;
        const uint64_t end = (uint64_t)data_offset + rd_u32(rec) + rd_u32(rec + 4);
        if (end > size) return false;
    }
    pack->data = data;
    pack->size = size;
    pack->count = count;
    pack->data_offset = data_offset;
    return true;
}

bool yz_glyph_decode(const yz_glyph_pack_t *pack, int index, uint8_t *dst) {
    if (!pack || !pack->data || index < 0 || index >= pack->count) {
        memset(dst, 0, YZ_GLYPH_PIXELS);
        return false;
    }
    const uint8_t *rec = pack->data + HEADER_SIZE + (size_t)index * RECORD_SIZE;
    const uint8_t *src = pack->data + pack->data_offset + rd_u32(rec);
    const uint8_t *end = src + rd_u32(rec + 4);
    size_t out = 0;
    while (src < end) {
        const uint8_t b = *src++;
        if (b < 0x80) {
            const size_t n = (size_t)b + 1;
            if (out + n > YZ_GLYPH_PIXELS) {
                out = YZ_GLYPH_PIXELS + 1;   // 标记损坏：游程超出画布
                break;
            }
            memset(dst + out, 0, n);
            out += n;
            continue;
        }
        const size_t n = (size_t)(b & 0x7F) + 1;
        const size_t bytes = (n + 1) / 2;
        if (out + n > YZ_GLYPH_PIXELS || (size_t)(end - src) < bytes) {
            out = YZ_GLYPH_PIXELS + 1;   // 标记损坏
            break;
        }
        for (size_t k = 0; k < n; k++) {
            const uint8_t byte = src[k / 2];
            const uint8_t v = (k & 1) ? (byte & 0x0F) : (byte >> 4);
            dst[out++] = (uint8_t)(v * 17);   // 0..15 → 0..255
        }
        src += bytes;
    }
    if (out != YZ_GLYPH_PIXELS || src != end) {
        memset(dst, 0, YZ_GLYPH_PIXELS);
        return false;
    }
    return true;
}
