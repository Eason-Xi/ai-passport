// tests/test_as_logic.c —— 设置存档、呼吸节奏、渐弱曲线、省电与低电量的主机测试。
#include <string.h>

#include "as_breath.h"
#include "as_cfg.h"
#include "as_power.h"
#include "as_test.h"

static void test_cfg_roundtrip(void) {
    as_cfg_t def, got;
    as_cfg_default(&def);
    CHECK(as_cfg_sanitize(&def));
    CHECK(as_cfg_audible(&def));
    as_cfg_t c = def;
    c.sound[0] = AS_SOUND_BOWL;
    c.sound[1] = AS_SOUND_NONE;
    c.sound[2] = AS_SOUND_BROWN;
    c.level[2] = 0;
    c.volume = 10;
    c.timer = AS_TIMER_90;
    c.breath = AS_BREATH_BOX;
    uint8_t buf[AS_CFG_BLOB_SIZE];
    CHECK(as_cfg_pack(&c, buf, sizeof buf - 1) == 0);
    CHECK(as_cfg_pack(&c, buf, sizeof buf) == AS_CFG_BLOB_SIZE);
    CHECK(as_cfg_unpack(&got, buf, sizeof buf));
    CHECK(memcmp(&got, &c, sizeof c) == 0);
    // 任意一个字节损坏都被 CRC 拦下，并回退到默认值。
    for (size_t i = 0; i < sizeof buf; i++) {
        uint8_t bad[AS_CFG_BLOB_SIZE];
        memcpy(bad, buf, sizeof bad);
        bad[i] ^= 0x10;
        CHECK(!as_cfg_unpack(&got, bad, sizeof bad));
        CHECK(memcmp(&got, &def, sizeof def) == 0);
    }
    CHECK(!as_cfg_unpack(&got, buf, sizeof buf - 1));
}

// CRC 正确但字段越界（例如未来版本写入、或手工构造）：整体拒绝。
static void test_cfg_out_of_range(void) {
    as_cfg_t c;
    as_cfg_default(&c);
    c.volume = 0;
    uint8_t buf[AS_CFG_BLOB_SIZE];
    as_cfg_pack(&c, buf, sizeof buf);
    as_cfg_t got;
    CHECK(!as_cfg_unpack(&got, buf, sizeof buf));
    as_cfg_default(&c);
    c.sound[1] = AS_SOUND_COUNT;   // 既不是声音也不是"空"
    CHECK(!as_cfg_sanitize(&c));
    CHECK(c.sound[1] == AS_SOUND_FIRE);
    c.level[0] = 11;
    c.timer = AS_TIMER_COUNT;
    c.breath = 9;
    CHECK(!as_cfg_sanitize(&c));
    CHECK(c.level[0] == 8 && c.timer == AS_TIMER_30 && c.breath == AS_BREATH_478);
    // 全部为空或音量为 0：不可听。
    for (int i = 0; i < AS_LAYERS; i++) c.sound[i] = AS_SOUND_NONE;
    CHECK(!as_cfg_audible(&c));
    c.sound[2] = AS_SOUND_RAIN;
    c.level[2] = 0;
    CHECK(!as_cfg_audible(&c));
}

static void test_volume_and_timer_tables(void) {
    CHECK(as_cfg_volume_percent(1) == 42);
    CHECK(as_cfg_volume_percent(10) == 100);
    CHECK(as_cfg_volume_percent(0) == 42);
    CHECK(as_cfg_volume_percent(50) == 100);
    for (uint8_t v = 2; v <= AS_VOLUME_MAX; v++) CHECK(as_cfg_volume_percent(v) > as_cfg_volume_percent(v - 1));
    const uint16_t want[AS_TIMER_COUNT] = { 0, 15, 30, 45, 60, 90 };
    for (uint8_t i = 0; i < AS_TIMER_COUNT; i++) CHECK(as_timer_minutes(i) == want[i]);
    CHECK(as_timer_minutes(AS_TIMER_COUNT) == 0);
}

static void test_breath_478(void) {
    CHECK(as_breath_cycle_ms(AS_BREATH_478) == 19000);
    as_breath_t b;
    as_breath_at(AS_BREATH_478, 0, &b);
    CHECK(b.phase == AS_PHASE_INHALE && b.remain_s == 4 && b.size == 0);
    int32_t prev = -1;
    for (uint32_t t = 0; t < 4000; t += 50) {
        as_breath_at(AS_BREATH_478, t, &b);
        CHECK(b.phase == AS_PHASE_INHALE);
        CHECK(b.size >= prev);   // 吸气：单调变大
        prev = b.size;
    }
    as_breath_at(AS_BREATH_478, 3999, &b);
    CHECK(b.remain_s == 1 && b.size > 32000);
    as_breath_at(AS_BREATH_478, 4000, &b);
    CHECK(b.phase == AS_PHASE_HOLD_IN && b.remain_s == 7 && b.size == 32767);
    as_breath_at(AS_BREATH_478, 11000, &b);
    CHECK(b.phase == AS_PHASE_EXHALE && b.remain_s == 8 && b.size == 32767);
    as_breath_at(AS_BREATH_478, 15000, &b);
    CHECK(b.phase == AS_PHASE_EXHALE && b.size > 15000 && b.size < 18000);   // 呼到一半
    as_breath_at(AS_BREATH_478, 19000 * 3 + 100, &b);
    CHECK(b.cycle == 3 && b.phase == AS_PHASE_INHALE);
}

static void test_breath_other_patterns(void) {
    as_breath_t b;
    CHECK(as_breath_cycle_ms(AS_BREATH_EVEN) == 10000);
    as_breath_at(AS_BREATH_EVEN, 5000, &b);
    CHECK(b.phase == AS_PHASE_EXHALE);   // 没有屏息阶段
    as_breath_at(AS_BREATH_EVEN, 9999, &b);
    CHECK(b.phase == AS_PHASE_EXHALE && b.size < 100);
    CHECK(as_breath_cycle_ms(AS_BREATH_BOX) == 16000);
    as_breath_at(AS_BREATH_BOX, 12500, &b);
    CHECK(b.phase == AS_PHASE_HOLD_OUT && b.size == 0 && b.remain_s == 4);
    // 越界节奏按 4-7-8 处理，不会越界访问。
    CHECK(as_breath_cycle_ms(99) == 19000);
}

static void test_fade_curve(void) {
    CHECK(as_timer_fade_gain(AS_FADE_MS) == 32768);
    CHECK(as_timer_fade_gain(10 * 60000u) == 32768);
    CHECK(as_timer_fade_gain(0) == 0);
    CHECK(as_timer_fade_gain(AS_FADE_MS / 2) == 8192);   // 平方曲线：剩一半时间 → 1/4
    int32_t prev = 0;
    for (uint32_t ms = 0; ms <= AS_FADE_MS; ms += 100) {
        const int32_t g = as_timer_fade_gain(ms);
        CHECK(g >= prev);
        prev = g;
    }
}

enum { UP = 0, DOWN = 1, OK = 2 };
enum { PRESS = 0, CLICK = 1, DOUBLE = 2, LONG = 3, RELEASE = 4 };

static void test_power_levels(void) {
    as_power_t p;
    as_power_init(&p, 1000);
    // 播放中：20 s 调暗、45 s 熄屏，不会关机。
    CHECK(!as_power_tick(&p, 1000 + AS_PLAY_DIM_MS - 1, AS_ACT_PLAYING));
    CHECK(as_power_tick(&p, 1000 + AS_PLAY_DIM_MS, AS_ACT_PLAYING) && p.level == AS_POWER_DIM);
    CHECK(as_power_backlight(&p) == AS_BACKLIGHT_DIM);
    CHECK(as_power_tick(&p, 1000 + AS_PLAY_OFF_MS, AS_ACT_PLAYING) && p.level == AS_POWER_SCREEN_OFF);
    CHECK(!as_power_tick(&p, 1000 + 5 * AS_IDLE_SHUTDOWN_MS, AS_ACT_PLAYING));
    CHECK(p.level == AS_POWER_SCREEN_OFF);
    // 停止后：从最近一次按键起 10 min 请求关机。
    CHECK(as_power_tick(&p, 1000 + AS_IDLE_SHUTDOWN_MS, AS_ACT_STOPPED) && p.level == AS_POWER_SHUTDOWN);
    // 保持常亮：不计时。
    as_power_init(&p, 0);
    CHECK(!as_power_tick(&p, 10 * AS_IDLE_SHUTDOWN_MS, AS_ACT_KEEP_ON));
    CHECK(!as_power_tick(&p, 10 * AS_IDLE_SHUTDOWN_MS + AS_IDLE_DIM_MS - 1, AS_ACT_STOPPED));
    // 强制熄屏后重新计时：10 min 后才关机。
    as_power_force(&p, AS_POWER_SCREEN_OFF, 50000);
    CHECK(as_power_backlight(&p) == AS_BACKLIGHT_OFF);
    CHECK(!as_power_tick(&p, 50000 + AS_IDLE_SHUTDOWN_MS - 1, AS_ACT_STOPPED));
    CHECK(as_power_tick(&p, 50000 + AS_IDLE_SHUTDOWN_MS, AS_ACT_STOPPED));
    as_power_wake(&p, 0);
    CHECK(p.level == AS_POWER_ON && as_power_backlight(&p) == AS_BACKLIGHT_ON);
}

static void test_power_wake_swallow(void) {
    as_power_t p;
    as_power_init(&p, 0);
    CHECK(!as_power_key(&p, OK, PRESS, 10));   // 亮屏时按键正常传递
    as_power_force(&p, AS_POWER_SCREEN_OFF, 100);
    CHECK(as_power_key(&p, OK, CLICK, 200));   // 熄屏时只有 PRESS 能唤醒
    CHECK(p.level == AS_POWER_SCREEN_OFF);
    CHECK(as_power_key(&p, OK, PRESS, 300) && p.level == AS_POWER_ON);
    CHECK(as_power_key(&p, OK, LONG, 800));     // 按住期间吞掉
    CHECK(as_power_key(&p, OK, RELEASE, 900));
    CHECK(as_power_key(&p, OK, CLICK, 1000));   // 松开后延迟到达的单击吞掉
    CHECK(!as_power_key(&p, OK, PRESS, 2000));  // 之后恢复正常
    // 唤醒后按别的键立即生效。
    as_power_force(&p, AS_POWER_DIM, 3000);
    CHECK(as_power_key(&p, UP, PRESS, 3100));
    CHECK(as_power_key(&p, UP, RELEASE, 3200));
    CHECK(!as_power_key(&p, DOWN, PRESS, 3300));
}

static void test_lowbatt(void) {
    as_lowbatt_t b;
    as_lowbatt_init(&b);
    CHECK(!as_lowbatt_feed(&b, 3, 3400, false));
    CHECK(!as_lowbatt_feed(&b, 2, 3390, false));
    CHECK(as_lowbatt_feed(&b, 2, 3380, false));
    as_lowbatt_init(&b);
    CHECK(!as_lowbatt_feed(&b, 1, 3400, false));
    CHECK(!as_lowbatt_feed(&b, 1, 3440, false));   // 电压上升：充电中，重新计数
    CHECK(!as_lowbatt_feed(&b, 1, 3440, false));
    CHECK(!as_lowbatt_feed(&b, 1, 3440, true));    // 接电脑 USB
    CHECK(!as_lowbatt_feed(&b, -1, -1, false));    // 读数不可用
    CHECK(!as_lowbatt_feed(&b, 4, 3500, false));
}

int main(void) {
    test_cfg_roundtrip();
    test_cfg_out_of_range();
    test_volume_and_timer_tables();
    test_breath_478();
    test_breath_other_patterns();
    test_fade_curve();
    test_power_levels();
    test_power_wake_swallow();
    test_lowbatt();
    printf("test_as_logic: PASS\n");
    return 0;
}
