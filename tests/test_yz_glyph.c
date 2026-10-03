// tests/test_yz_glyph.c —— 字形包解码：真实字形包逐字解码（与浮点双线性参考实现逐像素比对），
// 以及手工构造的原尺寸 / 缩小尺寸字形包与各种损坏数据。
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "yz_catalog.h"
#include "yz_glyph.h"
#include "yz_test.h"

static uint8_t s_out[YZ_GLYPH_PIXELS];

static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    CHECK(data != NULL);
    CHECK(fread(data, 1, *size, f) == *size);
    fclose(f);
    return data;
}

// 构造只含一个字形的最小字形包。
static size_t make_pack(uint8_t *buf, int edge, const uint8_t *glyph, uint32_t glyph_len) {
    memcpy(buf, "YZG1", 4);
    buf[4] = 1; buf[5] = 0;                                 // count
    buf[6] = (uint8_t)edge; buf[7] = (uint8_t)(edge >> 8);  // 存储边长
    buf[8] = 20; buf[9] = buf[10] = buf[11] = 0;            // data offset
    memset(buf + 12, 0, 4);                                 // offset 0
    buf[16] = (uint8_t)glyph_len; buf[17] = (uint8_t)(glyph_len >> 8); buf[18] = buf[19] = 0;
    memcpy(buf + 20, glyph, glyph_len);
    return 20 + glyph_len;
}

// 参考实现：先把游程完整解成 store × store 的 4 bit 像素，再按同一坐标约定做浮点双线性插值。
static void reference(const uint8_t *src, size_t len, int store, uint8_t *dst) {
    static uint8_t px[YZ_GLYPH_PIXELS];
    size_t out = 0;
    for (size_t i = 0; i < len;) {
        const uint8_t b = src[i++];
        if (b < 0x80) {
            for (int k = 0; k <= b; k++) px[out++] = 0;
        } else {
            const int n = (b & 0x7F) + 1;
            for (int k = 0; k < n; k++) px[out++] = (k & 1) ? (src[i + k / 2] & 0x0F) : (src[i + k / 2] >> 4);
            i += (size_t)(n + 1) / 2;
        }
    }
    CHECK(out == (size_t)store * store);
    for (int y = 0; y < YZ_GLYPH_SIZE; y++) {
        double sy = (y + 0.5) * store / YZ_GLYPH_SIZE - 0.5;
        sy = sy < 0 ? 0 : (sy > store - 1 ? store - 1 : sy);
        const int y0 = (int)sy, y1 = y0 + 1 < store ? y0 + 1 : store - 1;
        for (int x = 0; x < YZ_GLYPH_SIZE; x++) {
            double sx = (x + 0.5) * store / YZ_GLYPH_SIZE - 0.5;
            sx = sx < 0 ? 0 : (sx > store - 1 ? store - 1 : sx);
            const int x0 = (int)sx, x1 = x0 + 1 < store ? x0 + 1 : store - 1;
            const double fx = sx - x0, fy = sy - y0;
            const double top = px[y0 * store + x0] * (1 - fx) + px[y0 * store + x1] * fx;
            const double bot = px[y1 * store + x0] * (1 - fx) + px[y1 * store + x1] * fx;
            dst[y * YZ_GLYPH_SIZE + x] = (uint8_t)lround((top * (1 - fy) + bot * fy) * 17.0);
        }
    }
}

static void test_real_pack(const char *path) {
    size_t size;
    uint8_t *data = load(path, &size);
    yz_glyph_pack_t pack;
    CHECK(yz_glyph_pack_open(&pack, data, size));
    CHECK(pack.count == YZ_ENTRY_COUNT);
    CHECK(pack.store == 128);                   // 剪裱本原图分辨率，解码时放大到 176
    static uint8_t ref[YZ_GLYPH_PIXELS];
    for (int i = 0; i < pack.count; i++) {
        CHECK(yz_glyph_decode(&pack, i, s_out));
        int ink = 0, bright = 0;
        for (int p = 0; p < YZ_GLYPH_PIXELS; p++) {
            ink += s_out[p] > 0;
            bright += s_out[p] >= 200;
        }
        // 每个字都有笔画，也不会整块涂满（不超过 60%）。偈颂后的“其一”等小字与残字“刻”笔画少。
        CHECK(ink > YZ_GLYPH_PIXELS / 200);
        CHECK(ink < YZ_GLYPH_PIXELS * 6 / 10);
        CHECK(bright > 0);
        // 每隔 50 个字与参考实现逐像素比对（定点取整误差不超过 1）。
        if (i % 50 == 0 || i == pack.count - 1) {
            const uint8_t *rec = data + 12 + (size_t)i * 8;
            const uint32_t off = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) | ((uint32_t)rec[2] << 16) |
                                 ((uint32_t)rec[3] << 24);
            const uint32_t len = (uint32_t)rec[4] | ((uint32_t)rec[5] << 8) | ((uint32_t)rec[6] << 16) |
                                 ((uint32_t)rec[7] << 24);
            reference(data + pack.data_offset + off, len, pack.store, ref);
            for (int p = 0; p < YZ_GLYPH_PIXELS; p++) CHECK(abs((int)s_out[p] - (int)ref[p]) <= 1);
        }
    }
    CHECK(!yz_glyph_decode(&pack, pack.count, s_out));
    CHECK(!yz_glyph_decode(&pack, -1, s_out));
    // 截断：索引表指向包外时打开失败。
    CHECK(!yz_glyph_pack_open(&pack, data, size - 1));
    free(data);
}

// 用游程拼出 pixels 个像素：先 2 个字面像素 (15, 1)，其余全是 0。
static size_t make_glyph(uint8_t *glyph, int pixels) {
    size_t n = 0;
    glyph[n++] = 0x81;               // 2 个字面像素
    glyph[n++] = 0xF1;               // 15, 1
    int left = pixels - 2;
    while (left > 0) {
        const int run = left > 128 ? 128 : left;
        glyph[n++] = (uint8_t)(run - 1);
        left -= run;
    }
    return n;
}

static void test_synthetic(void) {
    static uint8_t buf[64 + YZ_GLYPH_PIXELS];
    yz_glyph_pack_t pack;
    uint8_t glyph[300];
    size_t n = make_glyph(glyph, YZ_GLYPH_PIXELS);

    // 原尺寸（存储边长 = 176）：逐像素直出。
    size_t size = make_pack(buf, YZ_GLYPH_SIZE, glyph, (uint32_t)n);
    CHECK(yz_glyph_pack_open(&pack, buf, size) && pack.store == YZ_GLYPH_SIZE);
    memset(s_out, 0xAA, sizeof s_out);
    CHECK(yz_glyph_decode(&pack, 0, s_out));
    CHECK(s_out[0] == 255 && s_out[1] == 17 && s_out[2] == 0 && s_out[YZ_GLYPH_PIXELS - 1] == 0);

    // 像素不足：少一段游程 → 失败且输出清零。
    size = make_pack(buf, YZ_GLYPH_SIZE, glyph, (uint32_t)(n - 1));
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    memset(s_out, 0xAA, sizeof s_out);
    CHECK(!yz_glyph_decode(&pack, 0, s_out));
    CHECK(s_out[0] == 0 && s_out[YZ_GLYPH_PIXELS - 1] == 0);

    // 像素过多：多一段游程 → 失败。
    glyph[n] = 0x05;
    size = make_pack(buf, YZ_GLYPH_SIZE, glyph, (uint32_t)(n + 1));
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    CHECK(!yz_glyph_decode(&pack, 0, s_out));

    // 字面像素声明的字节数超出该字数据 → 失败（不读越界）。
    const uint8_t trunc[] = { 0xFF, 0x11 };
    size = make_pack(buf, YZ_GLYPH_SIZE, trunc, sizeof trunc);
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    CHECK(!yz_glyph_decode(&pack, 0, s_out));

    // 缩小存储（边长 64）：放大后左上角是两个亮像素插值出的过渡，右下角为 0；与参考实现一致。
    n = make_glyph(glyph, 64 * 64);
    size = make_pack(buf, 64, glyph, (uint32_t)n);
    CHECK(yz_glyph_pack_open(&pack, buf, size) && pack.store == 64);
    CHECK(yz_glyph_decode(&pack, 0, s_out));
    static uint8_t ref[YZ_GLYPH_PIXELS];
    reference(glyph, n, 64, ref);
    int worst = 0;
    for (int p = 0; p < YZ_GLYPH_PIXELS; p++) {
        const int d = abs((int)s_out[p] - (int)ref[p]);
        worst = d > worst ? d : worst;
    }
    CHECK(worst <= 1);                          // 定点与浮点只差取整
    CHECK(s_out[0] == 255 && s_out[1] > s_out[3] && s_out[3] > 0 && s_out[YZ_GLYPH_PIXELS - 1] == 0);
    // 缩小存储同样拒绝像素不足 / 过多的数据。
    size = make_pack(buf, 64, glyph, (uint32_t)(n - 1));
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    memset(s_out, 0xAA, sizeof s_out);
    CHECK(!yz_glyph_decode(&pack, 0, s_out));
    CHECK(s_out[0] == 0 && s_out[YZ_GLYPH_PIXELS - 1] == 0);
    glyph[n] = 0x00;
    size = make_pack(buf, 64, glyph, (uint32_t)(n + 1));
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    CHECK(!yz_glyph_decode(&pack, 0, s_out));

    // 头部错误：魔数、边长超出范围（大于 176 或过小）。
    size = make_pack(buf, YZ_GLYPH_SIZE, glyph, (uint32_t)n);
    buf[0] = 'X';
    CHECK(!yz_glyph_pack_open(&pack, buf, size));
    buf[0] = 'Y';
    buf[6] = YZ_GLYPH_SIZE + 1;
    CHECK(!yz_glyph_pack_open(&pack, buf, size));
    buf[6] = YZ_GLYPH_MIN_STORE - 1;
    CHECK(!yz_glyph_pack_open(&pack, buf, size));
    CHECK(!yz_glyph_pack_open(&pack, NULL, 0));
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    test_synthetic();
    test_real_pack(argv[1]);
    printf("test_yz_glyph: PASS\n");
    return 0;
}
