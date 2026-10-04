// tests/test_mn_model.c —— 应用状态机主机测试：三键交互、长按连调、设置编辑、敲击测速、熄屏唤醒。
#include <stdint.h>
#include <stdio.h>

#include "mn_model.h"
#include "mn_sound.h"
#include "mn_test.h"

static mn_model_t m;
static uint32_t now;

static mn_fx_t key(mn_btn_t b, mn_ev_t e) {
    return mn_model_key(&m, b, e, now, (int64_t)now * 1000);
}

// 模拟一次完整单击：PRESS → RELEASE → （180 ms 后）CLICK。
static mn_fx_t click(mn_btn_t b) {
    mn_fx_t fx = key(b, MN_EV_PRESS);
    now += 80;
    fx |= key(b, MN_EV_RELEASE);
    now += 180;
    fx |= key(b, MN_EV_CLICK);
    return fx;
}

static mn_fx_t long_press(mn_btn_t b) {
    mn_fx_t fx = key(b, MN_EV_PRESS);
    now += 500;
    fx |= key(b, MN_EV_LONG);
    return fx;
}

static mn_fx_t advance(uint32_t ms) {
    mn_fx_t fx = 0;
    const uint32_t end = now + ms;
    while (now < end) {
        now += 10;
        fx |= mn_model_tick(&m, now);
    }
    return fx;
}

static void reset(void) {
    mn_cfg_t c;
    mn_cfg_default(&c);
    mn_model_init(&m, &c);
    now = 1000;
}

static void test_main_page(void) {
    reset();
    CHECK(m.page == MN_PAGE_MAIN && !m.running && m.cfg.bpm == 100);
    // UP/DOWN 按下立即 ±1；CLICK / DOUBLE 不重复计数。
    mn_fx_t fx = click(MN_BTN_UP);
    CHECK(m.cfg.bpm == 101 && (fx & MN_FX_METER) && (fx & MN_FX_SAVE));
    key(MN_BTN_DOWN, MN_EV_PRESS);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    key(MN_BTN_DOWN, MN_EV_DOUBLE);
    CHECK(m.cfg.bpm == 99);
    // OK 单击开始 / 停止；OK 按下本身无动作。
    CHECK(key(MN_BTN_OK, MN_EV_PRESS) == 0);
    fx = click(MN_BTN_OK);
    CHECK(m.running && (fx & MN_FX_RUN));
    click(MN_BTN_OK);
    CHECK(!m.running);
    // 边界夹紧。
    m.cfg.bpm = 250;
    CHECK(key(MN_BTN_UP, MN_EV_PRESS) == 0 && m.cfg.bpm == 250);
}

static void test_repeat(void) {
    reset();
    // 按下 +1，长按开始连调；松手立即停止。
    long_press(MN_BTN_UP);
    CHECK(m.rep_active && m.cfg.bpm == 102);   // PRESS +1，LONG 时第 0 次步进 +1
    advance(1000);
    const uint16_t after1s = m.cfg.bpm;
    CHECK(after1s >= 102 + 8);   // 前 1 秒每 120 ms 一步
    key(MN_BTN_UP, MN_EV_RELEASE);
    CHECK(!m.rep_active);
    advance(1000);
    CHECK(m.cfg.bpm == after1s);
    // 其它键的 RELEASE 不会停止连调。
    long_press(MN_BTN_DOWN);
    key(MN_BTN_OK, MN_EV_RELEASE);
    CHECK(m.rep_active);
    advance(5000);
    key(MN_BTN_DOWN, MN_EV_RELEASE);
    CHECK(m.cfg.bpm == 30);   // 一路降到下限
    // 丢失 RELEASE 时 15 s 保护上限。
    m.cfg.bpm = 30;
    long_press(MN_BTN_UP);
    advance(MN_REPEAT_GUARD_MS + 100);
    CHECK(!m.rep_active && m.cfg.bpm == 250);
    // 等待时间提示。
    reset();
    long_press(MN_BTN_UP);
    CHECK(mn_model_wait_ms(&m, now, 500) == 120);
    key(MN_BTN_UP, MN_EV_RELEASE);
    CHECK(mn_model_wait_ms(&m, now, 500) == 500);
}

static void test_settings(void) {
    reset();
    long_press(MN_BTN_OK);
    CHECK(m.page == MN_PAGE_SETTINGS && m.row == MN_ROW_BEATS && !m.editing);
    // 浏览：DOWN 下移、UP 上移，首尾循环。
    key(MN_BTN_UP, MN_EV_PRESS);
    CHECK(m.row == MN_ROW_TAP);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    CHECK(m.row == MN_ROW_BEATS);
    // 编辑拍号：UP 增加，夹紧 1..12，可长按连调。
    click(MN_BTN_OK);
    CHECK(m.editing);
    mn_fx_t fx = key(MN_BTN_UP, MN_EV_PRESS);
    CHECK(m.cfg.beats == 5 && (fx & MN_FX_METER));
    long_press(MN_BTN_UP);
    advance(3000);
    key(MN_BTN_UP, MN_EV_RELEASE);
    CHECK(m.cfg.beats == 12);
    click(MN_BTN_OK);
    CHECK(!m.editing);
    // 重音开关。
    key(MN_BTN_DOWN, MN_EV_PRESS);
    CHECK(m.row == MN_ROW_ACCENT);
    click(MN_BTN_OK);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    CHECK(m.cfg.accent == 0);
    // 重音行不支持长按连调：按下切换一次，LONG 不再连续切换。
    long_press(MN_BTN_UP);
    CHECK(!m.rep_active && m.cfg.accent == 1);
    click(MN_BTN_OK);
    // 细分循环 1→2→3→4→1，DOWN 反向。
    key(MN_BTN_DOWN, MN_EV_PRESS);
    click(MN_BTN_OK);
    for (int i = 0; i < 4; i++) key(MN_BTN_UP, MN_EV_PRESS);
    CHECK(m.cfg.subdiv == 1);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    CHECK(m.cfg.subdiv == 4);
    click(MN_BTN_OK);
    // 音色循环，发 SOUND 效果。
    key(MN_BTN_DOWN, MN_EV_PRESS);
    click(MN_BTN_OK);
    fx = key(MN_BTN_UP, MN_EV_PRESS);
    CHECK(m.cfg.sound == MN_SOUND_COWBELL && (fx & MN_FX_SOUND) && !(fx & MN_FX_METER));
    key(MN_BTN_UP, MN_EV_PRESS);
    CHECK(m.cfg.sound == MN_SOUND_BEEP);
    click(MN_BTN_OK);
    // 音量夹紧 0..10。
    key(MN_BTN_DOWN, MN_EV_PRESS);
    CHECK(m.row == MN_ROW_VOLUME);
    click(MN_BTN_OK);
    for (int i = 0; i < 12; i++) key(MN_BTN_DOWN, MN_EV_PRESS);
    CHECK(m.cfg.volume == 0);
    fx = key(MN_BTN_DOWN, MN_EV_PRESS);
    CHECK(fx == 0);   // 到底后不再产生保存
    fx = key(MN_BTN_UP, MN_EV_PRESS);
    CHECK(m.cfg.volume == 1 && (fx & MN_FX_VOLUME));
    // 编辑中 OK 长按直接返回主界面。
    long_press(MN_BTN_OK);
    CHECK(m.page == MN_PAGE_MAIN && !m.editing);
    // 节拍器在设置页里保持运行。
    click(MN_BTN_OK);
    long_press(MN_BTN_OK);
    CHECK(m.page == MN_PAGE_SETTINGS && m.running);
}

static void tap_at(uint32_t interval_ms, int count) {
    for (int i = 0; i < count; i++) {
        key(MN_BTN_OK, MN_EV_PRESS);
        now += 60;
        key(MN_BTN_OK, MN_EV_RELEASE);
        advance(interval_ms - 60);
    }
}

static void test_tap(void) {
    reset();
    click(MN_BTN_OK);
    CHECK(m.running);
    // 主界面 OK 双击进入，暂停播放。
    mn_fx_t fx = key(MN_BTN_OK, MN_EV_DOUBLE);
    CHECK(m.page == MN_PAGE_TAP && !m.running && (fx & MN_FX_RUN) && (fx & MN_FX_SCREEN));
    // 72 BPM 敲 5 下，2.5 s 后自动应用并恢复播放。
    tap_at(830, 4);   // advance 以 10 ms 步进，间隔取 10 的倍数
    key(MN_BTN_UP, MN_EV_PRESS);   // 任意键都算敲击
    CHECK(mn_tap_count(&m.tap) == 5);
    fx = advance(MN_TAP_APPLY_IDLE_MS + 20);
    CHECK(m.tap_done && m.cfg.bpm == 72 && (fx & MN_FX_SAVE) && (fx & MN_FX_METER));
    CHECK(m.page == MN_PAGE_TAP);
    fx = advance(MN_TAP_DONE_SHOW_MS + 20);
    CHECK(m.page == MN_PAGE_MAIN && m.running && (fx & MN_FX_RUN));

    // 少于 3 下：6 s 后放弃，BPM 不变。
    click(MN_BTN_OK);   // 停止
    key(MN_BTN_OK, MN_EV_DOUBLE);
    tap_at(500, 2);
    advance(MN_TAP_GIVEUP_MS + 20);
    CHECK(m.page == MN_PAGE_MAIN && m.cfg.bpm == 72 && !m.running);

    // OK 长按取消。
    key(MN_BTN_OK, MN_EV_DOUBLE);
    tap_at(400, 4);
    long_press(MN_BTN_OK);
    CHECK(m.page == MN_PAGE_MAIN && m.cfg.bpm == 72);

    // 从设置页的"敲击测速"行进入，结束后回到设置页。
    long_press(MN_BTN_OK);
    key(MN_BTN_UP, MN_EV_PRESS);
    CHECK(m.row == MN_ROW_TAP);
    click(MN_BTN_OK);
    CHECK(m.page == MN_PAGE_TAP);
    tap_at(500, 4);
    advance(MN_TAP_APPLY_IDLE_MS + MN_TAP_DONE_SHOW_MS + 50);
    CHECK(m.page == MN_PAGE_SETTINGS && m.cfg.bpm == 120);
    // 已设定提示期间的按键不再改变结果。
    key(MN_BTN_UP, MN_EV_PRESS);   // 设置页里这只是移动选中行
}

static void test_power(void) {
    mn_power_t p;
    now = 0;
    mn_power_init(&p, now);
    CHECK(mn_power_backlight(&p) == MN_BACKLIGHT_ON);
    // 播放中不降级，并从停止时刻重新计时。
    CHECK(!mn_power_tick(&p, 900000, true));
    const uint32_t stop = 900000;
    CHECK(!mn_power_tick(&p, stop + MN_DIM_AFTER_MS - 1, false));
    // 60 s 调暗 → 2 min 熄屏 → 10 min 请求关机，逐级只报告一次。
    CHECK(mn_power_tick(&p, stop + MN_DIM_AFTER_MS, false));
    CHECK(p.level == MN_POWER_DIM && mn_power_backlight(&p) == MN_BACKLIGHT_DIM);
    CHECK(!mn_power_tick(&p, stop + MN_DIM_AFTER_MS + 1000, false));
    CHECK(mn_power_tick(&p, stop + MN_SCREEN_OFF_AFTER_MS, false));
    CHECK(p.level == MN_POWER_SCREEN_OFF && mn_power_backlight(&p) == MN_BACKLIGHT_OFF);
    CHECK(!mn_power_tick(&p, stop + MN_POWER_OFF_AFTER_MS - 1, false));
    CHECK(mn_power_tick(&p, stop + MN_POWER_OFF_AFTER_MS, false));
    CHECK(p.level == MN_POWER_SHUTDOWN && mn_power_backlight(&p) == MN_BACKLIGHT_OFF);
    // 关机被取消：回到正常亮度并重新计时。
    mn_power_wake(&p, stop + MN_POWER_OFF_AFTER_MS);
    CHECK(p.level == MN_POWER_ON && mn_power_backlight(&p) == MN_BACKLIGHT_ON);
    CHECK(!mn_power_tick(&p, stop + MN_POWER_OFF_AFTER_MS + MN_DIM_AFTER_MS - 1, false));

    // 一次跳过多级（任务被耽搁）：直接进入对应等级。
    mn_power_init(&p, 0);
    CHECK(mn_power_tick(&p, MN_SCREEN_OFF_AFTER_MS + 5, false) && p.level == MN_POWER_SCREEN_OFF);

    // 熄屏时唤醒：这次按键的 PRESS / LONG / RELEASE / 延迟 CLICK 全部吞掉。
    now = 200000;
    CHECK(mn_power_key(&p, MN_BTN_OK, MN_EV_PRESS, now));
    CHECK(p.level == MN_POWER_ON && mn_power_backlight(&p) == MN_BACKLIGHT_ON);
    CHECK(mn_power_key(&p, MN_BTN_OK, MN_EV_LONG, now + 500));
    CHECK(mn_power_key(&p, MN_BTN_OK, MN_EV_RELEASE, now + 600));
    CHECK(mn_power_key(&p, MN_BTN_OK, MN_EV_CLICK, now + 780));
    // 之后的正常按键照常传递。
    CHECK(!mn_power_key(&p, MN_BTN_OK, MN_EV_PRESS, now + 2000));
    CHECK(!mn_power_key(&p, MN_BTN_OK, MN_EV_CLICK, now + 2200));
    // 调暗时唤醒后立即按其它键：不吞。
    mn_power_tick(&p, now + 2200 + MN_DIM_AFTER_MS, false);
    CHECK(p.level == MN_POWER_DIM);
    CHECK(mn_power_key(&p, MN_BTN_UP, MN_EV_PRESS, now + 70000));
    CHECK(!mn_power_key(&p, MN_BTN_DOWN, MN_EV_PRESS, now + 70100));
    CHECK(!mn_power_key(&p, MN_BTN_UP, MN_EV_RELEASE, now + 70200));
    // 按键重新计时：刚按过键不会关机。
    CHECK(!mn_power_tick(&p, now + 70200 + MN_DIM_AFTER_MS - 1, false));
}

static void test_lowbatt(void) {
    mn_lowbatt_t b;
    mn_lowbatt_init(&b);
    // 连续 3 次 ≤3% 才触发。
    CHECK(!mn_lowbatt_feed(&b, 3, 3400, false));
    CHECK(!mn_lowbatt_feed(&b, 2, 3390, false));
    CHECK(mn_lowbatt_feed(&b, 2, 3385, false));
    // 中途回到 4% 清零重计。
    mn_lowbatt_init(&b);
    CHECK(!mn_lowbatt_feed(&b, 1, 3300, false));
    CHECK(!mn_lowbatt_feed(&b, 4, 3420, false));
    CHECK(!mn_lowbatt_feed(&b, 1, 3300, false));
    CHECK(!mn_lowbatt_feed(&b, 1, 3300, false));
    CHECK(mn_lowbatt_feed(&b, 0, 3290, false));
    // 连接电脑 USB：不触发。
    mn_lowbatt_init(&b);
    for (int i = 0; i < 5; i++) CHECK(!mn_lowbatt_feed(&b, 1, 3300, true));
    // 电压上升超过 30 mV（插着充电头）：不触发。
    mn_lowbatt_init(&b);
    CHECK(!mn_lowbatt_feed(&b, 2, 3350, false));
    CHECK(!mn_lowbatt_feed(&b, 2, 3370, false));
    CHECK(!mn_lowbatt_feed(&b, 2, 3420, false));   // +70 mV → 判为充电，重新观察
    CHECK(b.count == 0);
    // 读数不可用：永不触发。
    mn_lowbatt_init(&b);
    for (int i = 0; i < 5; i++) CHECK(!mn_lowbatt_feed(&b, -1, -1, false));
    // 电压不可用但电量持续过低：仍按电量判定。
    for (int i = 0; i < 2; i++) CHECK(!mn_lowbatt_feed(&b, 1, -1, false));
    CHECK(mn_lowbatt_feed(&b, 1, -1, false));
}

int main(void) {
    test_main_page();
    test_repeat();
    test_settings();
    test_tap();
    test_power();
    test_lowbatt();
    puts("Metronome app model tests: PASS");
    return 0;
}
