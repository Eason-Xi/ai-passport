// main/mn_ui_main.c —— 主界面：速度术语、超大 BPM、节拍点 / 细分点、倒立摆杆、操作提示。
//
// 竖屏 240×320 自上而下：顶栏（mn_ui.c）→ 速度术语 → BPM 数字 → "BPM · 状态" →
// 节拍点（每小节 1–12 个，首拍琥珀色）→ 细分点 → 摆杆（支点在底部，每拍摆到一侧极点）→ 提示。
#include <math.h>
#include <stdio.h>

#include "mn_fonts.h"
#include "mn_layout.h"
#include "mn_strings.h"
#include "mn_tempo.h"
#include "mn_theme.h"
#include "mn_ui_internal.h"

#define TERM_Y 36
#define BPM_Y 66               // 大号数字字体行高很大，字形实际约占 y 66–136
#define STATE_Y 150
#define DOTS_CY 184
#define SUB_CY 199
#define SUB_D 6
#define SUB_PITCH 13
#define PIVOT_X 120
#define PIVOT_Y 283
#define ROD_LEN 72
#define BOB_D 16
#define BOB_POS 0.66f             // 摆锤位于摆杆的位置（从支点算起的比例）
#define ARC_R 77
#define HINT_Y 293

static lv_obj_t *s_term;
static lv_obj_t *s_bpm;
static lv_obj_t *s_state;
static lv_obj_t *s_dots[MN_BEATS_MAX];
static lv_obj_t *s_subs[MN_SUBDIV_MAX];
static lv_obj_t *s_rod;
static lv_obj_t *s_bob;
static lv_obj_t *s_hint;
static lv_point_precise_t s_rod_pts[2];

static uint8_t s_beats;           // 当前节拍点数量
static uint8_t s_subdiv;          // 当前细分点数量
static bool s_accent;
static int s_lit_beat = -1;       // 亮着的拍，-1 表示全灭
static uint8_t s_count;           // 正在显示的倒数秒数，0 表示未在倒数
static uint16_t s_bpm_val;        // 最近一次设置的 BPM（倒数结束后恢复显示）
static bool s_running;
static bool s_muted;

// 状态行：倒数中只显示"准备开始"（琥珀色）；否则"BPM · 演奏中 / 已停止"，静音时加后缀。
static void paint_state(void) {
    if (s_count) {
        lv_label_set_text(s_state, MN_STR_COUNTING);
        lv_obj_set_style_text_color(s_state, lv_color_hex(MN_C_ACCENT), 0);
        return;
    }
    const char *state = s_running ? MN_STR_RUNNING : MN_STR_STOPPED;
    if (s_muted) lv_label_set_text_fmt(s_state, MN_STR_BPM MN_STR_SEP "%s" MN_STR_SEP MN_STR_MUTE, state);
    else lv_label_set_text_fmt(s_state, MN_STR_BPM MN_STR_SEP "%s", state);
    lv_obj_set_style_text_color(s_state, lv_color_hex(s_running ? MN_C_BEAT : MN_C_TEXT_DIM), 0);
}
static int s_lit_sub = -1;
static int16_t s_last_angle = 32767;
static int s_last_flash = -1;

// 节拍点按数量重新排布并刷新颜色。
static void layout_dots(uint8_t beats) {
    mn_dot_t pos[MN_BEATS_MAX];
    mn_layout_dots(beats, pos);
    for (uint8_t i = 0; i < MN_BEATS_MAX; i++) {
        if (i >= beats) {
            lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_dots[i], pos[i].d, pos[i].d);
        lv_obj_set_pos(s_dots[i], pos[i].x - pos[i].d / 2, DOTS_CY - pos[i].d / 2);
    }
    s_beats = beats;
}

static void layout_subs(uint8_t subdiv) {
    const int left = PIVOT_X - (subdiv - 1) * SUB_PITCH / 2;
    for (uint8_t i = 0; i < MN_SUBDIV_MAX; i++) {
        if (subdiv < 2 || i >= subdiv) {
            lv_obj_add_flag(s_subs[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_subs[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_subs[i], left + i * SUB_PITCH - SUB_D / 2, SUB_CY - SUB_D / 2);
    }
    s_subdiv = subdiv;
}

// 首拍（且开启重音、拍数 > 1）用琥珀色，其余拍用青色；未亮的拍为暗灰，首拍外圈提示重音位置。
static void paint_dots(void) {
    for (uint8_t i = 0; i < s_beats; i++) {
        const bool accent = i == 0 && s_accent && s_beats > 1;
        const uint32_t on = accent ? MN_C_ACCENT : MN_C_BEAT;
        const bool lit = (int)i == s_lit_beat;
        lv_obj_set_style_bg_color(s_dots[i], lv_color_hex(lit ? on : MN_C_DOT_OFF), 0);
        lv_obj_set_style_border_width(s_dots[i], accent && !lit ? 2 : 0, 0);
        lv_obj_set_style_border_color(s_dots[i], mn_ui_mix(MN_C_DOT_OFF, MN_C_ACCENT, 150), 0);
    }
    for (uint8_t i = 0; i < s_subdiv; i++) {
        const bool lit = s_lit_beat >= 0 && (int)i <= s_lit_sub;
        lv_obj_set_style_bg_color(s_subs[i], lv_color_hex(lit ? MN_C_SUB : MN_C_DOT_OFF), 0);
    }
}

void mn_ui_main_create(lv_obj_t *scr) {
    s_term = mn_ui_label(scr, MN_FONT_BODY, MN_C_TEXT_DIM, "");
    lv_obj_align(s_term, LV_ALIGN_TOP_MID, 0, TERM_Y);

    s_bpm = mn_ui_label(scr, MN_FONT_BIG, MN_C_TEXT, "");
    lv_obj_align(s_bpm, LV_ALIGN_TOP_MID, 0, BPM_Y);

    s_state = mn_ui_label(scr, MN_FONT_SMALL, MN_C_TEXT_DIM, "");
    lv_obj_align(s_state, LV_ALIGN_TOP_MID, 0, STATE_Y);

    for (uint8_t i = 0; i < MN_BEATS_MAX; i++) s_dots[i] = mn_ui_box(scr, 0, 0, 10, 10, MN_C_DOT_OFF, LV_RADIUS_CIRCLE);
    for (uint8_t i = 0; i < MN_SUBDIV_MAX; i++) {
        s_subs[i] = mn_ui_box(scr, 0, 0, SUB_D, SUB_D, MN_C_DOT_OFF, LV_RADIUS_CIRCLE);
    }

    // 摆幅刻度弧：以支点为圆心、±30° 范围，提示摆杆的两个极点。
    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(arc, ARC_R * 2, ARC_R * 2);
    lv_obj_set_pos(arc, PIVOT_X - ARC_R, PIVOT_Y - ARC_R);
    lv_arc_set_bg_angles(arc, 237, 303);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(MN_C_DOT_OFF), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);

    s_rod = lv_line_create(scr);
    lv_obj_set_style_line_width(s_rod, 4, 0);
    lv_obj_set_style_line_rounded(s_rod, true, 0);
    lv_obj_set_style_line_color(s_rod, lv_color_hex(MN_C_ROD), 0);

    // 底座与支点。
    mn_ui_box(scr, PIVOT_X - 24, PIVOT_Y + 2, 48, 7, MN_C_PANEL_HI, 3);
    mn_ui_box(scr, PIVOT_X - 5, PIVOT_Y - 5, 10, 10, MN_C_ROD, LV_RADIUS_CIRCLE);

    s_bob = mn_ui_box(scr, 0, 0, BOB_D, BOB_D, MN_C_ROD, LV_RADIUS_CIRCLE);

    s_hint = mn_ui_label(scr, MN_FONT_SMALL, MN_C_TEXT_FAINT, "");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, HINT_Y);

    layout_dots(4);
    layout_subs(1);
    mn_ui_main_pendulum(0, 0, false);
}

void mn_ui_main_update(const mn_model_t *m) {
    const mn_cfg_t *c = &m->cfg;
    const uint8_t term = mn_tempo_term(c->bpm);
    lv_label_set_text_fmt(s_term, "%s" MN_STR_SEP "%s", MN_TEMPO_IT[term], MN_TEMPO_ZH[term]);
    s_bpm_val = c->bpm;
    s_running = m->running;
    if (!m->running) s_count = 0;
    if (s_count == 0) {
        lv_label_set_text_fmt(s_bpm, "%u", (unsigned)c->bpm);
        lv_obj_set_style_text_color(s_bpm, lv_color_hex(MN_C_TEXT), 0);
    }
    s_muted = c->volume == 0;
    paint_state();
    lv_label_set_text(s_hint, m->running ? MN_STR_HINT_RUNNING : MN_STR_HINT_STOPPED);

    s_accent = c->accent != 0;
    if (c->beats != s_beats) {
        layout_dots(c->beats);
        if (s_lit_beat >= (int)c->beats) s_lit_beat = -1;
    }
    // 运行中细分点跟随音频实际生效的细分（拍头才切换），停止时跟随设置。
    if (!m->running && c->subdiv != s_subdiv) layout_subs(c->subdiv);
    paint_dots();
}

// 倒数结束：恢复 BPM 数字与运行状态文字。
static void end_count(void) {
    if (!s_count) return;
    s_count = 0;
    lv_label_set_text_fmt(s_bpm, "%u", (unsigned)s_bpm_val);
    lv_obj_set_style_text_color(s_bpm, lv_color_hex(MN_C_TEXT), 0);
    paint_state();
}

void mn_ui_main_count(uint8_t remaining) {
    if (!s_running || remaining == 0) return;
    s_count = remaining;
    lv_label_set_text_fmt(s_bpm, "%u", (unsigned)remaining);
    lv_obj_set_style_text_color(s_bpm, lv_color_hex(MN_C_ACCENT), 0);
    paint_state();
}

void mn_ui_main_beat(const mn_beat_t *b) {
    end_count();
    if (b->beats != s_beats) layout_dots(b->beats);
    if (b->subdiv != s_subdiv) layout_subs(b->subdiv);
    s_lit_beat = b->beat;
    s_lit_sub = b->sub;
    paint_dots();
}

void mn_ui_main_stop(void) {
    end_count();
    s_lit_beat = -1;
    s_lit_sub = -1;
    paint_dots();
}

void mn_ui_main_pendulum(int16_t angle, int flash, bool accent) {
    if (angle == s_last_angle && flash == s_last_flash) return;
    s_last_angle = angle;
    s_last_flash = flash;
    const float rad = (float)angle * 3.14159265f / 1800.0f;
    const float sx = sinf(rad), cy = cosf(rad);
    s_rod_pts[0] = (lv_point_precise_t){ PIVOT_X, PIVOT_Y };
    s_rod_pts[1] = (lv_point_precise_t){ (lv_value_precise_t)lrintf(PIVOT_X + ROD_LEN * sx),
                                         (lv_value_precise_t)lrintf(PIVOT_Y - ROD_LEN * cy) };
    lv_line_set_points(s_rod, s_rod_pts, 2);
    const int bx = (int)lrintf(PIVOT_X + ROD_LEN * BOB_POS * sx);
    const int by = (int)lrintf(PIVOT_Y - ROD_LEN * BOB_POS * cy);
    lv_obj_set_pos(s_bob, bx - BOB_D / 2, by - BOB_D / 2);
    lv_obj_set_style_bg_color(s_bob, mn_ui_mix(MN_C_ROD, accent ? MN_C_ACCENT : MN_C_BEAT, flash), 0);
}
