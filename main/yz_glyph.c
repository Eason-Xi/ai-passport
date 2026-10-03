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
    if (edge < YZ_GLYPH_MIN_STORE || edge > YZ_GLYPH_SIZE || count == 0) return false;
    if ((size_t)HEADER_SIZE + (size_t)count * RECORD_SIZE > data_offset || data_offset > size) return false;
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *rec = data + HEADER_SIZE + (size_t)i * RECORD_SIZE;
        const uint64_t end = (uint64_t)data_offset + rd_u32(rec) + rd_u32(rec + 4);
        if (end > size) return false;
    }
    pack->data = data;
    pack->size = size;
    pack->count = count;
    pack->store = edge;
    pack->data_offset = data_offset;
    return true;
}

// ---- 游程流：按需读出若干个 4 bit 像素 ----

typedef struct {
    const uint8_t *src;
    const uint8_t *end;
    const uint8_t *lit;     // 当前字面段的数据
    uint8_t lit_pos;        // 当前字面段已读出的像素数
    uint8_t lit_len;        // 当前字面段的像素数
    uint8_t zeros;          // 当前 0 段还剩的像素数
} rle_t;

// 读 n 个像素（0..15）到 out。数据不足或字面段声明的字节越界时返回 false。
static bool rle_read(rle_t *r, uint8_t *out, size_t n) {
    while (n > 0) {
        if (r->zeros) {
            const size_t m = r->zeros < n ? r->zeros : n;
            memset(out, 0, m);
            out += m;
            n -= m;
            r->zeros = (uint8_t)(r->zeros - m);
            continue;
        }
        if (r->lit_pos < r->lit_len) {
            const uint8_t byte = r->lit[r->lit_pos / 2];
            *out++ = (r->lit_pos & 1) ? (byte & 0x0F) : (byte >> 4);
            r->lit_pos++;
            n--;
            continue;
        }
        if (r->src >= r->end) return false;
        const uint8_t b = *r->src++;
        if (b < 0x80) {
            r->zeros = (uint8_t)(b + 1);
            continue;
        }
        const size_t len = (size_t)(b & 0x7F) + 1;
        const size_t bytes = (len + 1) / 2;
        if ((size_t)(r->end - r->src) < bytes) return false;
        r->lit = r->src;
        r->lit_pos = 0;
        r->lit_len = (uint8_t)len;
        r->src += bytes;
    }
    return true;
}

// 游程正好用完：没有剩余像素，也没有多余字节。
static bool rle_done(const rle_t *r) {
    return r->zeros == 0 && r->lit_pos == r->lit_len && r->src == r->end;
}

// 插值权重的小数位数：12 位时 15 × 17 × 2^24 仍在 uint32 范围内，取整误差不超过 1。
#define FRAC_BITS 12
#define FRAC_ONE (1u << FRAC_BITS)

// 源坐标（FRAC_BITS 位小数定点）：s = (d + 0.5) × S / N − 0.5，夹在 [0, S − 1]。
static uint32_t src_coord(int d, int store) {
    const int32_t num = (2 * d + 1) * store - YZ_GLYPH_SIZE;    // s × 2N
    if (num <= 0) return 0;
    const uint32_t s = ((uint32_t)num << FRAC_BITS) / (2u * YZ_GLYPH_SIZE);
    const uint32_t max = (uint32_t)(store - 1) << FRAC_BITS;
    return s > max ? max : s;
}

// 每列的源坐标与权重只取决于存储边长，算一次缓存起来（不放在栈上：开机时解码跑在
// 栈很小的主任务里）。解码不可重入：同一时间只在一个任务里调用（固件里都在持有 LVGL 锁时）。
static uint8_t s_x0[YZ_GLYPH_SIZE];
static uint8_t s_x1[YZ_GLYPH_SIZE];
static uint16_t s_wx[YZ_GLYPH_SIZE];
static int s_table_store;

static void prepare_columns(int store) {
    if (s_table_store == store) return;
    for (int x = 0; x < YZ_GLYPH_SIZE; x++) {
        const uint32_t s = src_coord(x, store);
        const int i = (int)(s >> FRAC_BITS);
        s_x0[x] = (uint8_t)i;
        s_x1[x] = (uint8_t)(i + 1 < store ? i + 1 : store - 1);
        s_wx[x] = (uint16_t)(s & (FRAC_ONE - 1));
    }
    s_table_store = store;
}

// 逐行解码并双线性放大到 YZ_GLYPH_SIZE。缩放比 < 1，输出每前进一行，源行最多前进一行，
// 所以只需保留最近解码的两行（栈上约 350 字节）。
static bool decode_scaled(rle_t *r, int store, uint8_t *dst) {
    uint8_t rows[2][YZ_GLYPH_SIZE];
    prepare_columns(store);
    const uint8_t *x0 = s_x0, *x1 = s_x1;
    const uint16_t *wx = s_wx;
    int have = -1;      // 已解码到的源行
    for (int y = 0; y < YZ_GLYPH_SIZE; y++) {
        const uint32_t s = src_coord(y, store);
        const int y0 = (int)(s >> FRAC_BITS);
        const int y1 = y0 + 1 < store ? y0 + 1 : store - 1;
        const uint32_t wy = s & (FRAC_ONE - 1);
        while (have < y1) {
            have++;
            if (!rle_read(r, rows[have & 1], (size_t)store)) return false;
        }
        const uint8_t *a = rows[y0 & 1];
        const uint8_t *b = rows[y1 & 1];
        uint8_t *out = dst + (size_t)y * YZ_GLYPH_SIZE;
        for (int x = 0; x < YZ_GLYPH_SIZE; x++) {
            const uint32_t top = a[x0[x]] * (FRAC_ONE - wx[x]) + a[x1[x]] * wx[x];     // ≤ 15 × 2^12
            const uint32_t bot = b[x0[x]] * (FRAC_ONE - wx[x]) + b[x1[x]] * wx[x];
            const uint32_t v = top * (FRAC_ONE - wy) + bot * wy;                      // ≤ 15 × 2^24
            out[x] = (uint8_t)((v * 17u + (1u << (2 * FRAC_BITS - 1))) >> (2 * FRAC_BITS));   // 0..15 → 0..255
        }
    }
    // 其余源行也要读完，用来发现截断或多余的数据。
    while (have < store - 1) {
        have++;
        if (!rle_read(r, rows[have & 1], (size_t)store)) return false;
    }
    return true;
}

bool yz_glyph_decode(const yz_glyph_pack_t *pack, int index, uint8_t *dst) {
    if (!pack || !pack->data || index < 0 || index >= pack->count) {
        memset(dst, 0, YZ_GLYPH_PIXELS);
        return false;
    }
    const uint8_t *rec = pack->data + HEADER_SIZE + (size_t)index * RECORD_SIZE;
    const uint8_t *src = pack->data + pack->data_offset + rd_u32(rec);
    rle_t r = { .src = src, .end = src + rd_u32(rec + 4) };
    bool ok;
    if (pack->store == YZ_GLYPH_SIZE) {
        ok = rle_read(&r, dst, YZ_GLYPH_PIXELS);
        for (size_t i = 0; ok && i < YZ_GLYPH_PIXELS; i++) dst[i] = (uint8_t)(dst[i] * 17);
    } else {
        ok = decode_scaled(&r, pack->store, dst);
    }
    if (!ok || !rle_done(&r)) {
        memset(dst, 0, YZ_GLYPH_PIXELS);
        return false;
    }
    return true;
}
