// main/as_ui_home.c —— 聆听页（声景光环、混音名、层音量、音量浮层）与菜单抽屉。
#include <stdio.h>
#include <string.h>

#include "as_dsp.h"
#include "as_fonts.h"
#include "as_strings.h"
#include "as_theme.h"
#include "as_ui_internal.h"

#define ORB_CX 120
#define ORB_CY 124
#define CORE_D 62
#define BAR_W 46

static const int RING_BASE[AS_LAYERS] = { 178, 144, 110 };   // 外 → 内：轨道 1 → 3

static lv_obj_t *s_timer_text;
static lv_obj_t *s_batt_body, *s_batt_fill, *s_batt_tip, *s_batt_text;
static lv_obj_t *s_rings[AS_LAYERS];
static lv_obj_t *s_core, *s_play_glyph, *s_pause_bars;
static lv_obj_t *s_mix, *s_state;
static lv_obj_t *s_dots[AS_LAYERS], *s_fills[AS_LAYERS];
static lv_obj_t *s_volume, *s_volume_bar;
static lv_obj_t *s_rows[AS_MENU_COUNT];       // 菜单行
static lv_obj_t *s_row_bars[AS_MENU_COUNT];   // 菜单焦点条

// 动画快照（update 时拷贝，animate 时只读）。
static bool s_playing;
static bool s_active[AS_LAYERS];
static int s_ring_q4[AS_LAYERS];   // 当前显示直径 × 16（平滑）
static int s_core_q4;

void as_ui_home_forget(void) {
    s_timer_text = NULL;
    s_batt_body = s_batt_fill = s_batt_tip = s_batt_text = NULL;
    for (int i = 0; i < AS_LAYERS; i++) s_rings[i] = s_dots[i] = s_fills[i] = NULL;
    s_core = s_play_glyph = s_pause_bars = NULL;
    s_mix = s_state = s_volume = s_volume_bar = NULL;
    for (int i = 0; i < AS_MENU_COUNT; i++) s_rows[i] = s_row_bars[i] = NULL;
}

// ---- 顶栏 ----

static void status_build(lv_obj_t *root) {
    as_ui_moon(root, 14, 12, 14);
    s_timer_text = as_ui_label(root, AS_FONT_SMALL, AS_C_TEXT_DIM, "");
    lv_obj_set_pos(s_timer_text, 34, 9);

    // 电池图标：外框 22×11 + 正极 2×5，填充宽度随电量变化。
    s_batt_body = as_ui_box(root, 200, 13, 22, 11, -1, 3);
    lv_obj_set_style_border_width(s_batt_body, 1, 0);
    lv_obj_set_style_border_color(s_batt_body, lv_color_hex(AS_C_TEXT_DIM), 0);
    s_batt_fill = as_ui_box(s_batt_body, 2, 2, 16, 7, AS_C_OK, 1);
    s_batt_tip = as_ui_box(root, 222, 16, 2, 5, AS_C_TEXT_DIM, 1);
    s_batt_text = as_ui_label(root, AS_FONT_SMALL, AS_C_TEXT_DIM, "");
    lv_obj_set_style_text_align(s_batt_text, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_batt_text, 50);
    lv_obj_set_pos(s_batt_text, 146, 9);
}

void as_ui_home_set_battery(int soc) {
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
    lv_obj_set_style_bg_color(s_batt_fill, lv_color_hex(low ? AS_C_DANGER : AS_C_OK), 0);
    lv_obj_set_style_text_color(s_batt_text, lv_color_hex(low ? AS_C_DANGER : AS_C_TEXT_DIM), 0);
    lv_label_set_text_fmt(s_batt_text, "%d%%", soc);
}

// ---- 聆听页 ----

void as_ui_home_build(lv_obj_t *root, const as_model_t *m, bool audio_ok) {
    (void)m;
    (void)audio_ok;
    as_sine_init();
    status_build(root);

    for (int i = 0; i < AS_LAYERS; i++) {
        s_rings[i] = as_ui_circle(root, ORB_CX, ORB_CY, RING_BASE[i], -1);
        lv_obj_set_style_border_side(s_rings[i], LV_BORDER_SIDE_FULL, 0);
        s_ring_q4[i] = RING_BASE[i] * 16;
    }
    s_core = as_ui_circle(root, ORB_CX, ORB_CY, CORE_D, AS_C_PANEL_HI);
    s_core_q4 = CORE_D * 16;
    s_play_glyph = as_ui_label_center(root, AS_FONT_TITLE, AS_C_MOON, AS_STR_PLAY_GLYPH, ORB_CX - 30 + 3,
                                      ORB_CY - 21, 60);
    s_pause_bars = as_ui_box(root, ORB_CX - 10, ORB_CY - 11, 20, 22, -1, 0);
    as_ui_box(s_pause_bars, 0, 0, 6, 22, AS_C_MOON_DEEP, 2);
    as_ui_box(s_pause_bars, 14, 0, 6, 22, AS_C_MOON_DEEP, 2);

    s_mix = as_ui_label_center(root, AS_FONT_BODY, AS_C_TEXT, "", 8, 220, AS_SCREEN_W - 16);
    s_state = as_ui_label_center(root, AS_FONT_SMALL, AS_C_TEXT_DIM, "", 8, 246, AS_SCREEN_W - 16);

    // 三层音量小条：色点 + 细长进度。
    for (int i = 0; i < AS_LAYERS; i++) {
        const int x = 18 + i * 72;
        s_dots[i] = as_ui_circle(root, x + 4, 278, 8, AS_C_SOUND_NONE);
        as_ui_box(root, x + 12, 276, BAR_W, 4, AS_C_TRACK, 2);
        s_fills[i] = as_ui_box(root, x + 12, 276, 1, 4, AS_C_SOUND_NONE, 2);
    }
    as_ui_label_center(root, AS_FONT_SMALL, AS_C_TEXT_FAINT, AS_STR_HOME_HINT, 0, 294, AS_SCREEN_W);

    // 音量浮层（调音量时出现 1.5 s）。
    s_volume = as_ui_box(root, 28, 186, 184, 30, AS_C_PANEL, 15);
    lv_obj_set_style_border_width(s_volume, 1, 0);
    lv_obj_set_style_border_color(s_volume, lv_color_hex(AS_C_PANEL_HI), 0);
    lv_obj_t *vl = as_ui_label(s_volume, AS_FONT_SMALL, AS_C_TEXT_DIM, AS_STR_VOLUME);
    lv_obj_set_pos(vl, 14, 5);
    s_volume_bar = as_ui_segments(s_volume, 56, 10, 9, 10, 3);
    lv_obj_add_flag(s_volume, LV_OBJ_FLAG_HIDDEN);
}

static void format_left(char *buf, size_t n, uint32_t left_ms) {
    const uint32_t s = (left_ms + 999u) / 1000u;
    snprintf(buf, n, "%u:%02u", (unsigned)(s / 60u), (unsigned)(s % 60u));
}

void as_ui_home_update(const as_model_t *m, uint32_t now_ms, bool audio_ok) {
    if (!s_mix) return;
    const as_cfg_t *c = &m->cfg;

    if (m->timer_armed) {
        char buf[16];
        format_left(buf, sizeof buf, as_model_timer_left(m, now_ms));
        lv_label_set_text(s_timer_text, buf);
    } else {
        lv_label_set_text(s_timer_text, AS_STR_NO_TIMER);
    }

    // 混音名：可听的层按轨道顺序用 " · " 连接。
    char mix[64] = "";
    bool any = false;
    for (int i = 0; i < AS_LAYERS; i++) {
        s_active[i] = c->sound[i] != AS_SOUND_NONE && c->level[i] > 0;
        if (!s_active[i]) continue;
        if (any) strncat(mix, AS_STR_MIX_SEP, sizeof mix - strlen(mix) - 1);
        strncat(mix, AS_STR_SOUND[c->sound[i]], sizeof mix - strlen(mix) - 1);
        any = true;
    }
    lv_label_set_text(s_mix, any ? mix : AS_STR_NO_MIX);
    lv_obj_set_style_text_color(s_mix, lv_color_hex(any ? AS_C_TEXT : AS_C_TEXT_DIM), 0);

    s_playing = m->playing;
    if (!audio_ok) {
        lv_label_set_text(s_state, AS_STR_AUDIO_FAIL);
        lv_obj_set_style_text_color(s_state, lv_color_hex(AS_C_DANGER), 0);
    } else {
        lv_label_set_text(s_state, m->playing ? AS_STR_PLAYING : AS_STR_PAUSED);
        lv_obj_set_style_text_color(s_state, lv_color_hex(m->playing ? AS_C_MOON : AS_C_TEXT_DIM), 0);
    }

    for (int i = 0; i < AS_LAYERS; i++) {
        const uint32_t col = as_sound_color(c->sound[i]);
        lv_obj_set_style_border_color(s_rings[i], lv_color_hex(s_active[i] ? col : AS_C_SOUND_NONE), 0);
        lv_obj_set_style_border_width(s_rings[i], s_active[i] ? 3 : 1, 0);
        const int opa = s_active[i] ? (m->playing ? 130 + 12 * c->level[i] : 70 + 6 * c->level[i]) : 90;
        lv_obj_set_style_border_opa(s_rings[i], (lv_opa_t)(opa > 255 ? 255 : opa), 0);
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(col), 0);
        lv_obj_set_style_bg_color(s_fills[i], lv_color_hex(col), 0);
        const int w = s_active[i] ? BAR_W * c->level[i] / AS_LEVEL_MAX : 0;
        lv_obj_set_width(s_fills[i], w > 1 ? w : 1);
        if (w == 0) lv_obj_add_flag(s_fills[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_fills[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 中心按钮显示"按 OK 会发生什么"：播放中是暂停符号，暂停时是播放三角。
    lv_obj_set_style_bg_color(s_core, lv_color_hex(m->playing ? AS_C_MOON : AS_C_PANEL_HI), 0);
    if (m->playing) {
        lv_obj_add_flag(s_play_glyph, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_pause_bars, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_play_glyph, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pause_bars, LV_OBJ_FLAG_HIDDEN);
    }

    if (m->volume_shown) {
        lv_obj_remove_flag(s_volume, LV_OBJ_FLAG_HIDDEN);
        as_ui_segments_set(s_volume_bar, c->volume, AS_C_MOON);
    } else {
        lv_obj_add_flag(s_volume, LV_OBJ_FLAG_HIDDEN);
    }
}

// 光环：播放时随各层电平起伏，并叠加错开相位的缓慢"呼吸"；暂停时回到静止大小。
void as_ui_home_animate(uint32_t now_ms, const uint8_t meters[4]) {
    if (!s_rings[0]) return;
    for (int i = 0; i < AS_LAYERS; i++) {
        int target = RING_BASE[i] * 16;
        if (s_playing && s_active[i]) {
            const uint32_t phase = (uint32_t)((uint64_t)(now_ms + 1700u * (uint32_t)i) * 715827u);   // 约 6 s 一周
            target += meters[i] * 16 * 12 / 255 + as_sine(phase) * 16 * 3 / 32767;
        }
        const int prev = s_ring_q4[i] / 16;
        s_ring_q4[i] += (target - s_ring_q4[i]) / 4;
        const int d = s_ring_q4[i] / 16;
        if (d != prev) as_ui_circle_move(s_rings[i], ORB_CX, ORB_CY, d);
    }
    const int core_target = (CORE_D + (s_playing ? meters[3] * 8 / 255 : 0)) * 16;
    const int prev = s_core_q4 / 16;
    s_core_q4 += (core_target - s_core_q4) / 4;
    if (s_core_q4 / 16 != prev) as_ui_circle_move(s_core, ORB_CX, ORB_CY, s_core_q4 / 16);
}

// ---- 菜单抽屉 ----

#define MENU_Y 128
#define ROW_Y0 150
#define ROW_H 36
#define ROW_STEP 38

void as_ui_menu_build(lv_obj_t *root, const as_model_t *m) {
    lv_obj_t *scrim = as_ui_box(root, 0, 0, AS_SCREEN_W, AS_SCREEN_H, 0x000000, 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_50, 0);
    // 抽屉向下多出 20 px，把下方圆角藏在屏幕外。
    as_ui_box(root, 0, MENU_Y, AS_SCREEN_W, AS_SCREEN_H - MENU_Y + 20, AS_C_PANEL, 20);
    as_ui_box(root, 102, MENU_Y + 8, 36, 4, AS_C_TEXT_FAINT, 2);

    static const char *const LABELS[AS_MENU_COUNT] = {
        AS_STR_MENU_MIXER, AS_STR_MENU_TIMER, AS_STR_MENU_BREATH, AS_STR_MENU_BACK,
    };
    for (int i = 0; i < AS_MENU_COUNT; i++) {
        lv_obj_t *row = as_ui_box(root, 12, ROW_Y0 + i * ROW_STEP, AS_SCREEN_W - 24, ROW_H, AS_C_PANEL_HI, 12);
        s_rows[i] = row;
        s_row_bars[i] = as_ui_box(row, 0, 9, 4, ROW_H - 18, AS_C_MOON, 2);
        lv_obj_t *l = as_ui_label(row, AS_FONT_BODY, AS_C_TEXT, LABELS[i]);
        lv_obj_set_pos(l, 18, 6);
        lv_obj_t *v = as_ui_label(row, AS_FONT_SMALL, AS_C_TEXT_DIM, "");
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(v, 100);
        lv_obj_set_pos(v, AS_SCREEN_W - 24 - 112, 9);
        char buf[24] = "";
        if (i == AS_MENU_MIXER) {
            int n = 0;
            for (int k = 0; k < AS_LAYERS; k++) n += m->cfg.sound[k] != AS_SOUND_NONE && m->cfg.level[k] > 0;
            snprintf(buf, sizeof buf, "%d / %d", n, AS_LAYERS);
        } else if (i == AS_MENU_TIMER) {
            if (m->cfg.timer == AS_TIMER_OFF) snprintf(buf, sizeof buf, "%s", AS_STR_NO_TIMER);
            else snprintf(buf, sizeof buf, AS_STR_MINUTES_FMT, (unsigned)as_timer_minutes(m->cfg.timer));
        } else if (i == AS_MENU_BREATH) {
            snprintf(buf, sizeof buf, "%s", AS_STR_BREATH_SHORT[m->cfg.breath]);
        }
        lv_label_set_text(v, buf);
    }
    as_ui_menu_update(m);
}

void as_ui_menu_update(const as_model_t *m) {
    if (!s_rows[0]) return;
    for (int i = 0; i < AS_MENU_COUNT; i++) {
        const bool on = i == m->menu_sel;
        lv_obj_set_style_bg_opa(s_rows[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (on) lv_obj_remove_flag(s_row_bars[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_row_bars[i], LV_OBJ_FLAG_HIDDEN);
    }
}
