// tests/test_as_model.c —— 应用状态机的主机测试：页面流转、按键、播放与睡眠定时。
#include <string.h>

#include "as_model.h"
#include "as_test.h"

static as_model_t m;
static uint32_t now;

static as_fx_t key(as_btn_t b, as_ev_t e) {
    now += 50;
    return as_model_key(&m, b, e, now);
}

static as_fx_t tick_to(uint32_t t) {
    as_fx_t fx = 0;
    while (now < t) {
        now += 100;
        fx |= as_model_tick(&m, now);
    }
    return fx;
}

static void fresh(void) {
    as_cfg_t c;
    as_cfg_default(&c);
    now = 1000;
    as_model_init(&m, &c, now);
}

static void test_home_play_pause_volume(void) {
    fresh();
    CHECK(m.page == AS_PAGE_HOME && !m.playing);
    as_fx_t fx = key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.playing && (fx & AS_FX_AUDIO));
    fx = key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(!m.playing && (fx & AS_FX_AUDIO));
    // OK 的 PRESS / RELEASE 不触发任何功能（避免和单击重复）。
    CHECK(key(AS_BTN_OK, AS_EV_PRESS) == AS_FX_NONE);
    CHECK(key(AS_BTN_OK, AS_EV_RELEASE) == AS_FX_NONE);

    const uint8_t v0 = m.cfg.volume;
    fx = key(AS_BTN_UP, AS_EV_PRESS);
    CHECK(m.cfg.volume == v0 + 1 && (fx & AS_FX_VOLUME) && (fx & AS_FX_SAVE) && m.volume_shown);
    fx = key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.cfg.volume == v0 && (fx & AS_FX_VOLUME));
    // 音量浮层 1.5 s 后消失。
    fx = tick_to(now + AS_VOLUME_OVERLAY_MS + 100);
    CHECK(!m.volume_shown && (fx & AS_FX_REFRESH));
    // 夹在 1..10，越界时不再写存档。
    for (int i = 0; i < 20; i++) key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.cfg.volume == AS_VOLUME_MIN);
    fx = key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(!(fx & AS_FX_SAVE) && m.volume_shown);
}

static void test_hold_repeat(void) {
    fresh();
    m.cfg.volume = 2;
    key(AS_BTN_UP, AS_EV_PRESS);            // 3
    key(AS_BTN_UP, AS_EV_LONG);             // 4，开始连调
    CHECK(m.cfg.volume == 4 && m.hold_btn == AS_BTN_UP);
    CHECK(as_model_wait_ms(&m) == AS_TICK_FAST_MS);
    const uint32_t start = now;
    while (now < start + 3 * AS_HOLD_REPEAT_MS + 10) {
        now += 10;
        as_model_tick(&m, now);
    }
    CHECK(m.cfg.volume == 7);
    key(AS_BTN_DOWN, AS_EV_RELEASE);         // 别的键松开不影响
    CHECK(m.hold_btn == AS_BTN_UP);
    key(AS_BTN_UP, AS_EV_RELEASE);
    CHECK(m.hold_btn == -1 && as_model_wait_ms(&m) == AS_TICK_SLOW_MS);
    tick_to(now + 1000);
    CHECK(m.cfg.volume == 7);
}

static void test_menu_navigation(void) {
    fresh();
    as_fx_t fx = key(AS_BTN_OK, AS_EV_LONG);
    CHECK(m.page == AS_PAGE_MENU && (fx & AS_FX_SCREEN) && m.menu_sel == AS_MENU_MIXER);
    key(AS_BTN_UP, AS_EV_PRESS);             // 循环到最后一项
    CHECK(m.menu_sel == AS_MENU_BACK);
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.page == AS_PAGE_HOME);
    key(AS_BTN_OK, AS_EV_LONG);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.menu_sel == AS_MENU_TIMER);
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.page == AS_PAGE_TIMER && m.timer_sel == m.cfg.timer);
    key(AS_BTN_OK, AS_EV_LONG);              // 取消
    CHECK(m.page == AS_PAGE_HOME);
    key(AS_BTN_OK, AS_EV_LONG);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    key(AS_BTN_OK, AS_EV_DOUBLE);            // 双击在列表里等同于确定
    CHECK(m.page == AS_PAGE_BREATH);
    CHECK(as_model_keep_screen(&m, now));
    // 5 分钟后不再强制常亮（可能已经睡着）；切换节奏重新计时。
    CHECK(as_model_keep_screen(&m, now + AS_BREATH_KEEP_MS - 1));
    CHECK(!as_model_keep_screen(&m, now + AS_BREATH_KEEP_MS));
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.page == AS_PAGE_HOME && !as_model_keep_screen(&m, now));
    // 聆听页双击 OK 直达呼吸引导。
    key(AS_BTN_OK, AS_EV_DOUBLE);
    CHECK(m.page == AS_PAGE_BREATH);
    const uint8_t b0 = m.cfg.breath;
    fx = key(AS_BTN_UP, AS_EV_PRESS);
    CHECK(m.cfg.breath == (b0 + 1) % AS_BREATH_COUNT && (fx & AS_FX_SAVE) && m.breath_start_ms == now);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.cfg.breath == (b0 + AS_BREATH_COUNT - 1) % AS_BREATH_COUNT);
    key(AS_BTN_OK, AS_EV_LONG);
    CHECK(m.page == AS_PAGE_HOME);
}

static void open_mixer(void) {
    key(AS_BTN_OK, AS_EV_LONG);
    m.menu_sel = AS_MENU_MIXER;
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.page == AS_PAGE_MIXER && m.mix_sel == 0 && !m.mix_edit);
}

static void test_mixer_picker(void) {
    fresh();
    key(AS_BTN_OK, AS_EV_CLICK);   // 播放中试听
    open_mixer();
    key(AS_BTN_DOWN, AS_EV_PRESS);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.mix_sel == 2 && m.cfg.sound[2] == AS_SOUND_NONE);
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.page == AS_PAGE_PICKER && m.pick_sel == AS_PICK_NONE_INDEX && m.pick_orig == AS_SOUND_NONE);
    // ▼ 从"空"循环到第一个声音，并实时改动该层（试听）。
    as_fx_t fx = key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.pick_sel == 0 && m.cfg.sound[2] == AS_SOUND_RAIN && (fx & AS_FX_AUDIO));
    key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.cfg.sound[2] == AS_SOUND_WAVES);
    // 长按取消：恢复原来的"空"。
    fx = key(AS_BTN_OK, AS_EV_LONG);
    CHECK(m.page == AS_PAGE_MIXER && m.cfg.sound[2] == AS_SOUND_NONE && (fx & AS_FX_AUDIO));
    // 再进一次并确定：层音量为 0 时自动给 5 档，然后直接进入音量调节。
    m.cfg.level[2] = 0;
    key(AS_BTN_OK, AS_EV_CLICK);
    key(AS_BTN_UP, AS_EV_PRESS);              // 空 → 棕噪音
    CHECK(m.cfg.sound[2] == AS_SOUND_BROWN);
    fx = key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.page == AS_PAGE_MIXER && m.mix_edit && m.cfg.level[2] == 5 && (fx & AS_FX_SAVE));
    key(AS_BTN_UP, AS_EV_PRESS);
    CHECK(m.cfg.level[2] == 6);
    for (int i = 0; i < 10; i++) key(AS_BTN_UP, AS_EV_PRESS);
    CHECK(m.cfg.level[2] == AS_LEVEL_MAX);
    CHECK(key(AS_BTN_UP, AS_EV_PRESS) == AS_FX_NONE);
    key(AS_BTN_OK, AS_EV_CLICK);              // 结束调节
    CHECK(!m.mix_edit && m.page == AS_PAGE_MIXER);
    // 选择"空"并确定：不进入音量调节。
    key(AS_BTN_OK, AS_EV_CLICK);
    m.pick_sel = 0;
    key(AS_BTN_UP, AS_EV_PRESS);              // 0 → 空
    CHECK(m.cfg.sound[2] == AS_SOUND_NONE);
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(!m.mix_edit);
    // "完成"回到聆听页。
    key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(m.mix_sel == AS_MIX_DONE);
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.page == AS_PAGE_HOME);
}

static void test_play_without_sound_opens_mixer(void) {
    fresh();
    for (int i = 0; i < AS_LAYERS; i++) m.cfg.sound[i] = AS_SOUND_NONE;
    const as_fx_t fx = key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(!m.playing && m.page == AS_PAGE_MIXER && (fx & AS_FX_SCREEN));
}

static void test_sleep_timer(void) {
    fresh();
    m.cfg.timer = AS_TIMER_15;
    key(AS_BTN_OK, AS_EV_LONG);
    m.menu_sel = AS_MENU_TIMER;
    key(AS_BTN_OK, AS_EV_CLICK);
    key(AS_BTN_UP, AS_EV_PRESS);
    CHECK(m.timer_sel == AS_TIMER_OFF);
    CHECK(key(AS_BTN_UP, AS_EV_PRESS) == AS_FX_NONE);   // 到顶不循环
    key(AS_BTN_DOWN, AS_EV_PRESS);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    as_fx_t fx = key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.cfg.timer == AS_TIMER_30 && (fx & AS_FX_SAVE) && m.page == AS_PAGE_HOME);
    CHECK(as_model_timer_left(&m, now) == 30 * 60000u);

    key(AS_BTN_OK, AS_EV_CLICK);   // 播放：开始倒数
    const uint32_t t0 = now;
    tick_to(t0 + 10 * 60000u);
    CHECK_NEAR(as_model_timer_left(&m, now), 20 * 60000u, 200);
    key(AS_BTN_OK, AS_EV_CLICK);   // 暂停：冻结剩余时间
    const uint32_t frozen = as_model_timer_left(&m, now);
    tick_to(now + 5 * 60000u);
    CHECK(as_model_timer_left(&m, now) == frozen);
    key(AS_BTN_OK, AS_EV_CLICK);   // 继续
    // 剩 30 s 时处于渐弱中。
    tick_to(now + frozen - 30000u);
    CHECK(m.playing && m.fade > 4000 && m.fade < 12000);
    // 到点：停止播放，显示"晚安"，渐弱保持 0，定时重新装填。
    fx = tick_to(now + 31000u);
    CHECK(!m.playing && m.night && (fx & AS_FX_NIGHT) && (fx & AS_FX_AUDIO));
    CHECK(m.fade == 0 && m.timer_armed && as_model_timer_left(&m, now) == 30 * 60000u);
    CHECK(as_model_keep_screen(&m, now));
    fx = tick_to(now + AS_NIGHT_MS + 200);
    CHECK(!m.night && (fx & AS_FX_NIGHT_END));
    // 再次播放：渐弱恢复，重新倒数 30 分钟。
    key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.playing && m.fade == 32768);
    CHECK_NEAR(as_model_timer_left(&m, now), 30 * 60000u, 1);
}

static void test_timer_change_cancels_fade(void) {
    fresh();
    as_cfg_t c = m.cfg;
    c.timer = AS_TIMER_15;
    as_model_init(&m, &c, now);
    key(AS_BTN_OK, AS_EV_CLICK);
    tick_to(now + 15 * 60000u - 20000u);
    CHECK(m.fade < 32768);
    key(AS_BTN_OK, AS_EV_LONG);
    m.menu_sel = AS_MENU_TIMER;
    key(AS_BTN_OK, AS_EV_CLICK);
    m.timer_sel = AS_TIMER_OFF;
    const as_fx_t fx = key(AS_BTN_OK, AS_EV_CLICK);
    CHECK(m.fade == 32768 && (fx & AS_FX_AUDIO) && !m.timer_armed);
    tick_to(now + 60 * 60000u);
    CHECK(m.playing && !m.night);   // 关闭定时后一直播放
}

static void test_night_key_dismiss(void) {
    fresh();
    as_cfg_t c = m.cfg;
    c.timer = AS_TIMER_15;
    as_model_init(&m, &c, now);
    key(AS_BTN_OK, AS_EV_CLICK);
    tick_to(now + 15 * 60000u + 200);
    CHECK(m.night);
    CHECK(key(AS_BTN_OK, AS_EV_CLICK) == AS_FX_NONE);   // 非 PRESS 忽略
    const as_fx_t fx = key(AS_BTN_DOWN, AS_EV_PRESS);
    CHECK(!m.night && m.page == AS_PAGE_HOME && (fx & AS_FX_SCREEN));
    CHECK(m.cfg.volume == 5);   // 这次按键不会改音量
}

int main(void) {
    test_home_play_pause_volume();
    test_hold_repeat();
    test_menu_navigation();
    test_mixer_picker();
    test_play_without_sound_opens_mixer();
    test_sleep_timer();
    test_timer_change_cancels_fade();
    test_night_key_dismiss();
    CHECK(as_pick_to_sound(AS_PICK_NONE_INDEX) == AS_SOUND_NONE);
    CHECK(as_sound_to_pick(AS_SOUND_NONE) == AS_PICK_NONE_INDEX);
    CHECK(as_sound_to_pick(AS_SOUND_BOWL) == AS_SOUND_BOWL);
    printf("test_as_model: PASS\n");
    return 0;
}
