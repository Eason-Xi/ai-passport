// main/mn_ui.c —— 界面骨架：screen、顶栏（拍号摘要 + 电量）、页面切换与节拍动画调度。
#include "mn_ui.h"

#include <stdio.h>

#include "mn_fonts.h"
#include "mn_layout.h"
#include "mn_strings.h"
#include "mn_theme.h"
#include "mn_ui_internal.h"

#define PENDULUM_AMP 300          // 最大摆角 30.0°
#define FLASH_US 160000           // 节拍闪光衰减时长
#define TAP_FLASH_US 220000
#define LAG_COMP_US 24000         // 摆杆低通平滑带来的视觉滞后补偿

static lv_obj_t *s_scr;
static lv_obj_t *s_summary;       // 顶栏左：4/4 · 八分（拍号与细分；宽度须给右侧电量留位）
static lv_obj_t *s_batt_text;     // 顶栏右：85%
static lv_obj_t *s_batt_body;
static lv_obj_t *s_batt_fill;
static lv_obj_t *s_batt_tip;
static bool s_audio_ok = true;

// 节拍动画状态（只在 LVGL 上下文读写）。
static bool s_running;
static bool s_have_beat;          // 本次运行已经收到过拍头
static int64_t s_beat_us;         // 最近一个拍头的发声时刻
static int64_t s_beat_len_us;     // 一拍时长
static uint32_t s_beat_no;
static int64_t s_flash_us;        // 最近一次闪光的开始时刻
static bool s_flash_accent;
static int64_t s_tap_flash_us = INT64_MIN / 2;
static float s_angle;             // 摆杆显示角度（平滑后）

lv_obj_t *mn_ui_box(lv_obj_t *parent, int x, int y, int w, int h, int32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    if (color >= 0) {
        lv_obj_set_style_bg_color(o, lv_color_hex((uint32_t)color), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

lv_obj_t *mn_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_color_t mn_ui_mix(uint32_t a, uint32_t b, int t) {
    if (t < 0) t = 0;
    if (t > 255) t = 255;
    return lv_color_mix(lv_color_hex(b), lv_color_hex(a), (uint8_t)t);
}

// ---- 顶栏 ----

static void topbar_create(void) {
    s_summary = mn_ui_label(s_scr, MN_FONT_SMALL, MN_C_TEXT_DIM, "");
    lv_obj_set_pos(s_summary, 30, 11);

    // 电池图标：外框 22×11 + 正极 2×5，填充宽度随电量变化。
    s_batt_body = mn_ui_box(s_scr, 184, 15, 22, 11, -1, 3);
    lv_obj_set_style_border_width(s_batt_body, 1, 0);
    lv_obj_set_style_border_color(s_batt_body, lv_color_hex(MN_C_TEXT_DIM), 0);
    s_batt_fill = mn_ui_box(s_batt_body, 2, 2, 16, 7, MN_C_OK, 1);
    s_batt_tip = mn_ui_box(s_scr, 206, 18, 2, 5, MN_C_TEXT_DIM, 1);
    s_batt_text = mn_ui_label(s_scr, MN_FONT_SMALL, MN_C_TEXT_DIM, "");
    lv_obj_set_style_text_align(s_batt_text, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_batt_text, 50);
    lv_obj_set_pos(s_batt_text, 130, 11);
    mn_ui_set_battery(-1);
}

static void topbar_update(const mn_model_t *m) {
    if (!s_audio_ok) {
        lv_obj_set_style_text_color(s_summary, lv_color_hex(MN_C_DANGER), 0);
        lv_label_set_text(s_summary, MN_STR_AUDIO_FAIL);
        return;
    }
    const mn_cfg_t *c = &m->cfg;
    lv_obj_set_style_text_color(s_summary, lv_color_hex(MN_C_TEXT_DIM), 0);
    lv_label_set_text_fmt(s_summary, "%u/4" MN_STR_SEP "%s", (unsigned)c->beats, MN_SUBDIV_NAME[c->subdiv - 1]);
}

void mn_ui_set_battery(int soc) {
    if (!s_batt_body) return;
    const bool show = soc >= 0;
    lv_obj_t *const parts[] = { s_batt_body, s_batt_tip, s_batt_text };
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
        if (show) lv_obj_remove_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!show) return;
    if (soc > 100) soc = 100;
    const bool low = soc <= 20;
    lv_obj_set_width(s_batt_fill, soc * 16 / 100 > 1 ? soc * 16 / 100 : 1);
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(low ? MN_C_DANGER : MN_C_OK), 0);
    lv_obj_set_style_text_color(s_batt_text, lv_color_hex(low ? MN_C_DANGER : MN_C_TEXT_DIM), 0);
    lv_label_set_text_fmt(s_batt_text, "%d%%", soc);
}

// ---- 页面 ----

void mn_ui_init(bool audio_ok) {
    s_audio_ok = audio_ok;
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(MN_C_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    mn_ui_main_create(s_scr);
    topbar_create();
    lv_screen_load(s_scr);
}

void mn_ui_show(const mn_model_t *m) {
    if (m->page != MN_PAGE_TAP && mn_ui_tap_is_open()) mn_ui_tap_close();
    // 从设置页进入测速时设置面板保留在下面，只有回到主界面才关闭。
    if (m->page == MN_PAGE_MAIN && mn_ui_settings_is_open()) mn_ui_settings_close();
    if (m->page == MN_PAGE_SETTINGS && !mn_ui_settings_is_open()) mn_ui_settings_open(s_scr, true);
    if (m->page == MN_PAGE_TAP && !mn_ui_tap_is_open()) {
        s_tap_flash_us = INT64_MIN / 2;   // 不继承上一次测速的闪光
        mn_ui_tap_open(s_scr);
    }
    mn_ui_update(m);
}

void mn_ui_update(const mn_model_t *m) {
    topbar_update(m);
    if (s_running != m->running) {
        s_running = m->running;
        s_have_beat = false;
        if (!s_running) mn_ui_main_stop();
    }
    mn_ui_main_update(m);
    if (mn_ui_settings_is_open()) mn_ui_settings_update(m);
    if (mn_ui_tap_is_open()) mn_ui_tap_update(m);
}

void mn_ui_tap_flash(int64_t now_us) {
    s_tap_flash_us = now_us;
}

// ---- 节拍动画 ----

void mn_ui_beat(const mn_beat_t *b, int64_t now_us) {
    (void)now_us;
    if (!s_running) return;
    mn_ui_main_beat(b);
    if (b->sub != 0) return;
    s_have_beat = true;
    s_beat_us = b->play_us;
    s_beat_len_us = b->bpm ? 60000000LL / b->bpm : 1000000;
    s_beat_no = b->beat_no;
    s_flash_us = b->play_us;
    s_flash_accent = b->kind == MN_TICK_ACCENT;
}

// 闪光强度：发声瞬间 255，线性衰减到 0。
static int flash_level(int64_t now_us, int64_t start_us, int64_t len_us) {
    const int64_t dt = now_us - start_us;
    if (dt < 0 || dt >= len_us) return 0;
    return (int)(255 - dt * 255 / len_us);
}

void mn_ui_animate(int64_t now_us) {
    float target = 0.0f;
    int flash = 0;
    if (s_running && s_have_beat) {
        int64_t dt = now_us + LAG_COMP_US - s_beat_us;
        if (dt < 0) dt = 0;
        uint32_t frac = (uint32_t)(dt * 65536 / s_beat_len_us);
        uint32_t beat_no = s_beat_no;
        if (frac >= 65536u) {
            // 下一个拍头的事件还没到（或刚停止）：停在下一侧极点，不越过。
            frac = 0;
            beat_no++;
        }
        target = (float)mn_pendulum_angle(beat_no, frac, PENDULUM_AMP);
        flash = flash_level(now_us, s_flash_us, FLASH_US);
    } else if (s_running) {
        // 已开始、第一拍尚未发声：先摆到第一拍所在的左侧极点。
        target = (float)-PENDULUM_AMP;
    }
    // 一阶低通：开始 / 停止时平滑过渡，稳态下的滞后由 LAG_COMP_US 补偿。
    s_angle += (target - s_angle) * 0.55f;
    if (s_angle - target < 0.5f && target - s_angle < 0.5f) s_angle = target;
    // 覆盖层打开时摆杆被完全遮住：不重绘它（省 CPU），关闭后的下一帧自然追上当前位置。
    if (!mn_ui_settings_is_open() && !mn_ui_tap_is_open()) {
        mn_ui_main_pendulum((int16_t)s_angle, flash, s_flash_accent);
    }
    if (mn_ui_settings_is_open()) mn_ui_settings_led(flash, s_flash_accent);
    if (mn_ui_tap_is_open()) mn_ui_tap_ring(flash_level(now_us, s_tap_flash_us, TAP_FLASH_US));
}
