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
    in.current = YZ_ENTRY_COUNT - 1;
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) in.count[i] = (uint8_t)(i * 37);
    in.fav[0] = 0x81;
    in.fav[YZ_FAV_BYTES - 1] = (uint8_t)((1u << (YZ_ENTRY_COUNT % 8 ? YZ_ENTRY_COUNT % 8 : 8)) - 1);
    in.sessions = 123456;
    in.seconds = 0x89ABCDEFu;
    CHECK(yz_prog_pack(&in, buf, YZ_PROG_BLOB_SIZE) == YZ_PROG_BLOB_SIZE);
    yz_progress_t out;
    yz_progress_reset(&out);
    CHECK(yz_prog_unpack(&out, buf, YZ_PROG_BLOB_SIZE));
    CHECK(memcmp(&in, &out, sizeof in) == 0);

    yz_progress_t probe;
    yz_progress_reset(&probe);
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE + 1));

    // 字数变了（新版字目）：旧进度整体作废。
    buf[3] = (uint8_t)(YZ_ENTRY_COUNT + 1);
    const uint32_t crc = yz_crc32(buf, YZ_PROG_BLOB_SIZE - 4);
    memcpy(buf + YZ_PROG_BLOB_SIZE - 4, &crc, 4);                 // 主机为小端
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));

    // current 越界。
    in.current = YZ_ENTRY_COUNT;
    yz_prog_pack(&in, buf, YZ_PROG_BLOB_SIZE);
    CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));

    // 收藏位图里超出字数的位被置位。
    if (YZ_ENTRY_COUNT % 8) {
        in.current = 0;
        in.fav[YZ_FAV_BYTES - 1] = 0x80;
        yz_prog_pack(&in, buf, YZ_PROG_BLOB_SIZE);
        CHECK(!yz_prog_unpack(&probe, buf, YZ_PROG_BLOB_SIZE));
    }
    yz_progress_t zero;
    yz_progress_reset(&zero);
    CHECK(memcmp(&probe, &zero, sizeof zero) == 0);
}

int main(void) {
    test_crc();
    test_cfg();
    test_prog();
    printf("test_yz_save: PASS\n");
    return 0;
}
