// tests/test_mn_cfg.c —— 设置默认值、范围修正、存档编解码与损坏检测。
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mn_cfg.h"
#include "mn_crc32.h"
#include "mn_sound.h"
#include "mn_test.h"

int main(void) {
    // CRC-32 标准校验值。
    CHECK(mn_crc32("123456789", 9) == 0xCBF43926u);

    mn_cfg_t c;
    mn_cfg_default(&c);
    CHECK(c.bpm == 100 && c.beats == 4 && c.accent == 1 && c.subdiv == 1);
    CHECK(c.sound == MN_SOUND_WOOD && c.volume == 7 && c.countin == 1);
    CHECK(mn_cfg_sanitize(&c));

    // 往返。
    mn_cfg_t in = { .bpm = 187, .beats = 7, .accent = 0, .subdiv = 3, .sound = MN_SOUND_COWBELL, .volume = 0,
                    .countin = 0 };
    uint8_t buf[MN_CFG_BLOB_SIZE];
    CHECK(mn_cfg_pack(&in, buf, sizeof buf - 1) == 0);
    CHECK(mn_cfg_pack(&in, buf, sizeof buf) == MN_CFG_BLOB_SIZE);
    mn_cfg_t out;
    CHECK(mn_cfg_unpack(&out, buf, sizeof buf));
    CHECK(memcmp(&in, &out, sizeof in) == 0);
    const mn_meter_t m = mn_cfg_meter(&out);
    CHECK(m.bpm == 187 && m.beats == 7 && m.subdiv == 3 && !m.accent);

    // 每个字节翻转都应被拒绝，且回落默认值。
    mn_cfg_t def;
    mn_cfg_default(&def);
    for (size_t i = 0; i < sizeof buf; i++) {
        uint8_t bad[MN_CFG_BLOB_SIZE];
        memcpy(bad, buf, sizeof bad);
        bad[i] ^= 0x5A;
        CHECK(!mn_cfg_unpack(&out, bad, sizeof bad));
        CHECK(memcmp(&out, &def, sizeof def) == 0);
    }
    CHECK(!mn_cfg_unpack(&out, buf, sizeof buf - 1));

    // CRC 正确但字段越界：拒绝。
    mn_cfg_t wild = in;
    wild.bpm = 400;
    mn_cfg_pack(&wild, buf, sizeof buf);
    CHECK(!mn_cfg_unpack(&out, buf, sizeof buf));

    // sanitize 逐项回落。
    wild = (mn_cfg_t){ .bpm = 10, .beats = 13, .accent = 2, .subdiv = 0, .sound = 9, .volume = 11, .countin = 7 };
    CHECK(!mn_cfg_sanitize(&wild));
    CHECK(memcmp(&wild, &def, sizeof def) == 0);

    // v1 旧存档（第 10 字节为保留 0、无开始倒数字段）：读取成功，开始倒数取默认开启。
    uint8_t v1[MN_CFG_BLOB_SIZE] = { 'M', 'N', 1, 120, 0, 3, 1, 2, MN_SOUND_BEEP, 5, 0 };
    const uint32_t crc = mn_crc32(v1, 11);
    for (int i = 0; i < 4; i++) v1[11 + i] = (uint8_t)(crc >> (8 * i));
    CHECK(mn_cfg_unpack(&out, v1, sizeof v1));
    CHECK(out.bpm == 120 && out.beats == 3 && out.subdiv == 2 && out.sound == MN_SOUND_BEEP && out.volume == 5);
    CHECK(out.countin == 1);
    // 新存档写成 v2，"关闭"能保存下来；未知版本拒绝。
    mn_cfg_pack(&out, buf, sizeof buf);
    CHECK(buf[2] == MN_CFG_VERSION);
    out.countin = 0;
    mn_cfg_pack(&out, buf, sizeof buf);
    CHECK(mn_cfg_unpack(&out, buf, sizeof buf) && out.countin == 0);
    v1[2] = 3;
    CHECK(!mn_cfg_unpack(&out, v1, sizeof v1));

    // 音量映射：0 静音，1–10 单调递增到 100%。
    CHECK(mn_cfg_volume_percent(0) == 0 && mn_cfg_volume_percent(10) == 100);
    for (uint8_t v = 1; v < MN_VOLUME_MAX; v++) CHECK(mn_cfg_volume_percent(v) < mn_cfg_volume_percent(v + 1));
    CHECK(mn_cfg_volume_percent(50) == 100);
    puts("Metronome settings tests: PASS");
    return 0;
}
