// tests/test_yz_glyph.c —— 字形包解码：真实字形包逐字解码，以及手工构造的损坏数据。
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
static size_t make_pack(uint8_t *buf, const uint8_t *glyph, uint32_t glyph_len) {
    memcpy(buf, "YZG1", 4);
    buf[4] = 1; buf[5] = 0;                                 // count
    buf[6] = YZ_GLYPH_SIZE; buf[7] = 0;                     // edge
    buf[8] = 20; buf[9] = buf[10] = buf[11] = 0;            // data offset
    memset(buf + 12, 0, 4);                                 // offset 0
    buf[16] = (uint8_t)glyph_len; buf[17] = (uint8_t)(glyph_len >> 8); buf[18] = buf[19] = 0;
    memcpy(buf + 20, glyph, glyph_len);
    return 20 + glyph_len;
}

static void test_real_pack(const char *path) {
    size_t size;
    uint8_t *data = load(path, &size);
    yz_glyph_pack_t pack;
    CHECK(yz_glyph_pack_open(&pack, data, size));
    CHECK(pack.count == YZ_ENTRY_COUNT);
    for (int i = 0; i < pack.count; i++) {
        CHECK(yz_glyph_decode(&pack, i, s_out));
        int ink = 0, full = 0;
        for (int p = 0; p < YZ_GLYPH_PIXELS; p++) {
            CHECK(s_out[p] % 17 == 0);          // 4 bit → 8 bit 映射
            ink += s_out[p] > 0;
            full += s_out[p] == 255;
        }
        // 每个字都有笔画（至少 1%），也不会整块涂满（不超过 60%）。
        CHECK(ink > YZ_GLYPH_PIXELS / 100);
        CHECK(ink < YZ_GLYPH_PIXELS * 6 / 10);
        CHECK(full > 0);
    }
    CHECK(!yz_glyph_decode(&pack, pack.count, s_out));
    CHECK(!yz_glyph_decode(&pack, -1, s_out));
    // 截断：索引表指向包外时打开失败。
    CHECK(!yz_glyph_pack_open(&pack, data, size - 1));
    free(data);
}

static void test_synthetic(void) {
    static uint8_t buf[64 + YZ_GLYPH_PIXELS];
    yz_glyph_pack_t pack;
    // 30976 个像素：先 2 个字面像素 (15, 1)，再 242 段 128 个 0 减去 2 个……用游程拼满。
    uint8_t glyph[300];
    size_t n = 0;
    glyph[n++] = 0x81;               // 2 个字面像素
    glyph[n++] = 0xF1;               // 15, 1
    int left = YZ_GLYPH_PIXELS - 2;
    while (left > 0) {
        const int run = left > 128 ? 128 : left;
        glyph[n++] = (uint8_t)(run - 1);
        left -= run;
    }
    size_t size = make_pack(buf, glyph, (uint32_t)n);
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    memset(s_out, 0xAA, sizeof s_out);
    CHECK(yz_glyph_decode(&pack, 0, s_out));
    CHECK(s_out[0] == 255 && s_out[1] == 17 && s_out[2] == 0 && s_out[YZ_GLYPH_PIXELS - 1] == 0);

    // 像素不足：少一段游程 → 失败且输出清零。
    size = make_pack(buf, glyph, (uint32_t)(n - 1));
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    memset(s_out, 0xAA, sizeof s_out);
    CHECK(!yz_glyph_decode(&pack, 0, s_out));
    CHECK(s_out[0] == 0 && s_out[YZ_GLYPH_PIXELS - 1] == 0);

    // 像素过多：多一段游程 → 失败。
    glyph[n] = 0x05;
    size = make_pack(buf, glyph, (uint32_t)(n + 1));
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    CHECK(!yz_glyph_decode(&pack, 0, s_out));

    // 字面像素声明的字节数超出该字数据 → 失败（不读越界）。
    const uint8_t trunc[] = { 0xFF, 0x11 };
    size = make_pack(buf, trunc, sizeof trunc);
    CHECK(yz_glyph_pack_open(&pack, buf, size));
    CHECK(!yz_glyph_decode(&pack, 0, s_out));

    // 头部错误。
    size = make_pack(buf, glyph, (uint32_t)n);
    buf[0] = 'X';
    CHECK(!yz_glyph_pack_open(&pack, buf, size));
    buf[0] = 'Y';
    buf[6] = 64;                     // 边长不符
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
