// main/mn_ui_settings.c —— 设置面板：从底部滑出，覆盖顶栏以下区域；节拍器继续运行。
//
// 面板头：标题 + 当前 BPM + 随节拍闪烁的指示灯（让用户在调整时仍能"看见"节拍）。
// 六行：拍号 / 首拍重音 / 细分 / 音色 / 音量（附条形）/ 敲击测速。
// 选中行用浅底 + 左侧青色竖条；编辑中的值变为琥珀色并带 ◀ ▶。
#include <stdio.h>

#include "mn_fonts.h"
#include "mn_strings.h"
#include "mn_theme.h"
#include "mn_ui_internal.h"

#define PANEL_Y 36              // 刚好盖住主界面的速度术语，只露出顶栏
#define PANEL_H (320 - PANEL_Y)
#define ROW_Y0 42
#define ROW_H 31                // 7 行 + 底部提示刚好放进面板
#define ROW_X 12
#define ROW_W 216
#define LED_D 10

static lv_obj_t *s_panel;
static lv_obj_t *s_head_bpm;
static lv_obj_t *s_led;
static lv_obj_t *s_rows[MN_ROW_COUNT];
static lv_obj_t *s_marks[MN_ROW_COUNT];
static lv_obj_t *s_labels[MN_ROW_COUNT];
static lv_obj_t *s_values[MN_ROW_COUNT];
static lv_obj_t *s_vol_bar;
static lv_obj_t *s_hint;
static int s_last_led = -1;

bool mn_ui_settings_is_open(void) {
    return s_panel != NULL;
}

void mn_ui_settings_open(lv_obj_t *scr, bool animate) {
    s_panel = mn_ui_box(scr, 0, PANEL_Y, 240, PANEL_H + 20, MN_C_PANEL, 18);

    lv_obj_t *title = mn_ui_label(s_panel, MN_FONT_BODY, MN_C_TEXT, MN_STR_SETTINGS);
    lv_obj_set_pos(title, 24, 9);
    s_led = mn_ui_box(s_panel, 206, 17, LED_D, LED_D, MN_C_DOT_OFF, LV_RADIUS_CIRCLE);
    s_head_bpm = mn_ui_label(s_panel, MN_FONT_SMALL, MN_C_TEXT_DIM, "");
    lv_obj_set_style_text_align(s_head_bpm, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_head_bpm, 100);
    lv_obj_set_pos(s_head_bpm, 98, 12);

    for (int i = 0; i < MN_ROW_COUNT; i++) {
        s_rows[i] = mn_ui_box(s_panel, ROW_X, ROW_Y0 + i * ROW_H, ROW_W, ROW_H - 4, -1, 10);
        s_marks[i] = mn_ui_box(s_rows[i], 0, 6, 3, ROW_H - 16, MN_C_BEAT, 2);
        s_labels[i] = mn_ui_label(s_rows[i], MN_FONT_BODY, MN_C_TEXT_DIM, MN_ROW_LABEL[i]);
        lv_obj_align(s_labels[i], LV_ALIGN_LEFT_MID, 12, 0);
        s_values[i] = mn_ui_label(s_rows[i], MN_FONT_BODY, MN_C_TEXT, "");
        lv_obj_align(s_values[i], LV_ALIGN_RIGHT_MID, -12, 0);
    }

    // 音量条：10 档，放在数值左侧。
    s_vol_bar = lv_bar_create(s_rows[MN_ROW_VOLUME]);
    lv_obj_remove_style_all(s_vol_bar);
    lv_obj_set_size(s_vol_bar, 62, 6);
    lv_obj_align(s_vol_bar, LV_ALIGN_RIGHT_MID, -48, 0);
    lv_bar_set_range(s_vol_bar, 0, MN_VOLUME_MAX);
    lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(MN_C_DOT_OFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vol_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(MN_C_BEAT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_vol_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_vol_bar, 3, LV_PART_INDICATOR);

    s_hint = mn_ui_label(s_panel, MN_FONT_SMALL, MN_C_TEXT_FAINT, "");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, ROW_Y0 + MN_ROW_COUNT * ROW_H + 6);
    s_last_led = -1;

    if (animate) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_panel);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_set_values(&a, 320, PANEL_Y);
        lv_anim_set_duration(&a, 180);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

void mn_ui_settings_close(void) {
    if (!s_panel) return;
    lv_obj_delete(s_panel);   // 同时删除以它为对象的滑入动画
    s_panel = NULL;
}

// 第 row 行的显示值。
static void value_text(const mn_cfg_t *c, int row, char *buf, size_t len) {
    switch (row) {
    case MN_ROW_BEATS: snprintf(buf, len, "%u/4", (unsigned)c->beats); break;
    case MN_ROW_ACCENT: snprintf(buf, len, "%s", c->accent ? MN_STR_ON : MN_STR_OFF); break;
    case MN_ROW_COUNTIN: snprintf(buf, len, "%s", c->countin ? MN_STR_ON : MN_STR_OFF); break;
    case MN_ROW_SUBDIV: snprintf(buf, len, "%s", MN_SUBDIV_NAME[c->subdiv - 1]); break;
    case MN_ROW_SOUND: snprintf(buf, len, "%s", MN_SOUND_NAME[c->sound]); break;
    case MN_ROW_VOLUME:
        if (c->volume) snprintf(buf, len, "%u", (unsigned)c->volume);
        else snprintf(buf, len, "%s", MN_STR_MUTE);
        break;
    default: snprintf(buf, len, "%s", MN_STR_ENTER); break;
    }
}

void mn_ui_settings_update(const mn_model_t *m) {
    if (!s_panel) return;
    const mn_cfg_t *c = &m->cfg;
    lv_label_set_text_fmt(s_head_bpm, "%u " MN_STR_BPM, (unsigned)c->bpm);
    for (int i = 0; i < MN_ROW_COUNT; i++) {
        const bool sel = i == m->row;
        const bool edit = sel && m->editing;
        lv_obj_set_style_bg_color(s_rows[i], lv_color_hex(MN_C_PANEL_HI), 0);
        lv_obj_set_style_bg_opa(s_rows[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_rows[i], edit ? 1 : 0, 0);
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(MN_C_ACCENT), 0);
        lv_obj_set_style_bg_color(s_marks[i], lv_color_hex(edit ? MN_C_ACCENT : MN_C_BEAT), 0);
        if (sel) lv_obj_remove_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_labels[i], lv_color_hex(sel ? MN_C_TEXT : MN_C_TEXT_DIM), 0);

        char value[32];
        value_text(c, i, value, sizeof value);
        if (edit) lv_label_set_text_fmt(s_values[i], MN_STR_EDIT_L "%s" MN_STR_EDIT_R, value);
        else lv_label_set_text(s_values[i], value);
        uint32_t color = sel ? MN_C_TEXT : MN_C_TEXT_DIM;
        if (edit) color = MN_C_ACCENT;
        if (i == MN_ROW_TAP) color = sel ? MN_C_BEAT : MN_C_TEXT_FAINT;
        lv_obj_set_style_text_color(s_values[i], lv_color_hex(color), 0);
    }
    // 编辑音量时数值两侧多了箭头，条形相应左移。
    lv_bar_set_value(s_vol_bar, c->volume, LV_ANIM_OFF);
    const bool vol_edit = m->row == MN_ROW_VOLUME && m->editing;
    lv_obj_align(s_vol_bar, LV_ALIGN_RIGHT_MID, vol_edit ? -84 : -50, 0);
    lv_obj_set_style_bg_color(s_vol_bar, lv_color_hex(vol_edit ? MN_C_ACCENT : MN_C_BEAT), LV_PART_INDICATOR);

    const char *hint = MN_STR_HINT_BROWSE;
    if (m->editing) hint = MN_STR_HINT_EDIT;
    else if (m->row == MN_ROW_TAP) hint = MN_STR_HINT_TAP_ROW;
    lv_label_set_text(s_hint, hint);
}

void mn_ui_settings_led(int flash, bool accent) {
    if (!s_panel || flash == s_last_led) return;
    s_last_led = flash;
    lv_obj_set_style_bg_color(s_led, mn_ui_mix(MN_C_DOT_OFF, accent ? MN_C_ACCENT : MN_C_BEAT, flash), 0);
}
