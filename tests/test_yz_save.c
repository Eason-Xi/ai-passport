// tests/test_yz_save.c —— 设置与进度存档：往返、CRC、版本、越界取值、字数变化。
#include <string.h>

#include "yz_save.h"
#include "yz_test.h"

static void test_crc(void) {
    // CRC-32/ISO-HDLC 标准校验值。
    CHECK(yz_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);
}

static void test_cfg(void) {
    uint8_t buf[YZ_CFG_BLOB_SIZE];
    yz_cfg_t in = { .grid = YZ_GRID_JIU, .ink = YZ_INK_TRACE, .timer_idx = 4, .auto_next = 0, .sound = 1,
                    .bright_idx = 3 };
    CHECK(yz_cfg_pack(&in, buf, sizeof buf - 1) == 0);
    CHECK(yz_cfg_pack(&in, buf, sizeof buf) == YZ_CFG_BLOB_SIZE);
    yz_cfg_t out;
    memset(&out, 0xFF, sizeof out);
    CHECK(yz_cfg_unpack(&out, buf, sizeof buf));
    CHECK(memcmp(&in, &out, sizeof in) == 0);

    yz_cfg_t keep;
    yz_cfg_default(&keep);
    yz_cfg_t probe = keep;
    CHECK(!yz_cfg_unpack(&probe, buf, sizeof buf - 1));          // 长度不符
    buf[3] ^= 1;                                                  // 改一个字节 → CRC 失败
    CHECK(!yz_cfg_unpack(&probe, buf, sizeof buf));
    CHECK(memcmp(&probe, &keep, sizeof keep) == 0);               // 失败时不改动输出

    in.grid = YZ_GRID_COUNT;                                      // 越界取值（CRC 正确）
    yz_cfg_pack(&in, buf, sizeof buf);
    CHECK(!yz_cfg_unpack(&probe, buf, sizeof buf));
    in.grid = 0;
    in.bright_idx = YZ_BRIGHT_OPTIONS;
    yz_cfg_pack(&in, buf, sizeof buf);
    CHECK(!yz_cfg_unpack(&probe, buf, sizeof buf));
    in.bright_idx = 0;
    yz_cfg_pack(&in, buf, sizeof buf);
    buf[2] = 2;                                                   // 版本不符
    CHECK(!yz_cfg_unpack(&probe, buf, sizeof buf));
}

static void test_prog(void) {
    static uint8_t buf[YZ_PROG_BLOB_SIZE + 4];
    yz_progress_t in;
    yz_progress_reset(&in);
    in.book = YZ_BOOK_COUNT - 1;
    for (int k = 0; k < YZ_BOOK_COUNT; k++) {
        in.current[k] = (uint16_t)(YZ_BOOKS[k].first_entry + YZ_BOOKS[k].entry_count - 1);
        in.sessions[k] = 123456u + (uint32_t)k;
        in.seconds[k] = 0x89ABCDEFu - (uint32_t)k;
    }
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) in.count[i] = (uint8_t)(i * 37);
    in.fav[0] = 0x81;
    in.fav[YZ_FAV_BYTES - 1] = (uint8_t)((1u << (YZ_ENTRY_COUNT % 8 ? YZ_ENTRY_COUNT % 8 : 8)) - 1);
    CHECK(yz_prog_pack(&in, buf, YZ_PROG_BLOB_SIZE - 1) == 0);
    CHECK(yz_prog_pack(&in, buf, YZ_PROG_BLOB_SIZE) == YZ_PROG_BLOB_SIZE);
    yz_progress_t out;
    yz_progress_reset(&out);
    CHECK(yz_prog_unpack(&out, buf, YZ_PROG_BLOB_SIZE));
    CHECK(memcmp(&in, &out, sizeof in) == 0);

    yz_progress_t probe, keep;
    yz_progress_reset(&probe);
    keep = probe;
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE + 1));
    buf[10] ^= 1;                                                 // CRC 失败
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));

    // 字帖数或字数变了：旧进度整体作废（CRC 正确也不行）。
    yz_prog_pack(&in, buf, YZ_PROG_BLOB_SIZE);
    buf[3] = (uint8_t)(YZ_BOOK_COUNT + 1);
    uint32_t crc = yz_crc32(buf, YZ_PROG_BLOB_SIZE - 4);
    memcpy(buf + YZ_PROG_BLOB_SIZE - 4, &crc, 4);                 // 主机为小端
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));
    yz_prog_pack(&in, buf, YZ_PROG_BLOB_SIZE);
    buf[4] = (uint8_t)(YZ_ENTRY_COUNT + 1);
    crc = yz_crc32(buf, YZ_PROG_BLOB_SIZE - 4);
    memcpy(buf + YZ_PROG_BLOB_SIZE - 4, &crc, 4);
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));

    // 当前字帖越界；某帖记住的字不属于该帖。
    yz_progress_t bad = in;
    bad.book = YZ_BOOK_COUNT;
    yz_prog_pack(&bad, buf, YZ_PROG_BLOB_SIZE);
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));
    bad = in;
    bad.current[1] = 0;
    yz_prog_pack(&bad, buf, YZ_PROG_BLOB_SIZE);
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));

    // 收藏位图里超出字数的位被置位。
    if (YZ_ENTRY_COUNT % 8) {
        bad = in;
        bad.fav[YZ_FAV_BYTES - 1] = 0x80;
        yz_prog_pack(&bad, buf, YZ_PROG_BLOB_SIZE);
        CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));
    }
    CHECK(memcmp(&probe, &keep, sizeof keep) == 0);              // 失败时不改动输出
}

// 只有《多宝塔碑》的旧固件写下的 v1 进度：全部迁移到第一本字帖，其余字帖从零开始。
static size_t make_v1(uint8_t *buf, int n, int current, uint32_t sessions, uint32_t seconds) {
    const int fav_bytes = (n + 7) / 8;
    uint8_t *p = buf;
    *p++ = 'Y';
    *p++ = 'P';
    *p++ = 1;
    *p++ = (uint8_t)n;
    *p++ = (uint8_t)(n >> 8);
    *p++ = (uint8_t)current;
    *p++ = (uint8_t)(current >> 8);
    for (int i = 0; i < n; i++) *p++ = (uint8_t)(i % 5);
    memset(p, 0, (size_t)fav_bytes);
    p[0] = 0x06;                                                  // 收藏第 1、2 字
    p[(n - 1) / 8] |= (uint8_t)(1u << ((n - 1) % 8));            // 收藏最后一字
    p += fav_bytes;
    memcpy(p, &sessions, 4);
    memcpy(p + 4, &seconds, 4);
    p += 8;
    const uint32_t crc = yz_crc32(buf, (size_t)(p - buf));
    memcpy(p, &crc, 4);
    return (size_t)(p - buf) + 4;
}

static void test_prog_v1_migration(void) {
    static uint8_t buf[YZ_PROG_READ_MAX];
    const int n = YZ_BOOKS[0].entry_count;                        // 旧固件的字数
    size_t len = make_v1(buf, n, 74, 86, 5160);
    CHECK(len <= sizeof buf);
    yz_progress_t out;
    memset(&out, 0xEE, sizeof out);
    CHECK(yz_prog_unpack(&out, buf, len));
    CHECK(out.book == 0 && out.current[0] == 74 && out.sessions[0] == 86 && out.seconds[0] == 5160);
    for (int k = 1; k < YZ_BOOK_COUNT; k++) {
        CHECK(out.current[k] == YZ_BOOKS[k].first_entry && out.sessions[k] == 0 && out.seconds[k] == 0);
    }
    for (int i = 0; i < n; i++) CHECK(out.count[i] == (uint8_t)(i % 5));
    for (int i = n; i < YZ_ENTRY_COUNT; i++) CHECK(out.count[i] == 0);
    CHECK((out.fav[0] & 0x07) == 0x06 && ((out.fav[(n - 1) / 8] >> ((n - 1) % 8)) & 1u));
    for (int i = n; i < YZ_ENTRY_COUNT; i++) CHECK(((out.fav[i / 8] >> (i % 8)) & 1u) == 0);

    // 迁移后再按 v2 存取，结果不变。
    static uint8_t v2[YZ_PROG_BLOB_SIZE];
    yz_prog_pack(&out, v2, sizeof v2);
    yz_progress_t again;
    CHECK(yz_prog_unpack(&again, v2, sizeof v2) && memcmp(&again, &out, sizeof out) == 0);

    // 拒绝：字数超过第一本、当前字越界、长度不符、CRC 错误。
    yz_progress_t probe;
    yz_progress_reset(&probe);
    const yz_progress_t keep = probe;
    len = make_v1(buf, n + 1, 0, 0, 0);
    CHECK(!yz_prog_unpack(&probe, buf, len));
    len = make_v1(buf, n, n, 0, 0);
    CHECK(!yz_prog_unpack(&probe, buf, len));
    len = make_v1(buf, n, 0, 0, 0);
    CHECK(!yz_prog_unpack(&probe, buf, len - 1));
    buf[8] ^= 1;
    CHECK(!yz_prog_unpack(&probe, buf, len));
    buf[8] ^= 1;
    buf[2] = 9;                                                   // 未知版本
    CHECK(!yz_prog_unpack(&probe, buf, len));
    CHECK(memcmp(&probe, &keep, sizeof keep) == 0);
}

int main(void) {
    test_crc();
    test_cfg();
    test_prog();
    test_prog_v1_migration();
    printf("test_yz_save: PASS\n");
    return 0;
}
