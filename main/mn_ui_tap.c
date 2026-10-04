// main/mn_ui_tap.c —— 敲击测速页：全屏覆盖。圆环中央是实时 BPM，每次敲击圆环闪一下。
#include <stdio.h>

#include "mn_fonts.h"
#include "mn_strings.h"
#include "mn_tempo.h"
#include "mn_theme.h"
#include "mn_ui_internal.h"

#define RING_D 156
#define RING_CY 138

static lv_obj_t *s_page;
static lv_obj_t *s_ring;
static lv_obj_t *s_bpm;
static lv_obj_t *s_unit;
static lv_obj_t *s_count;
static lv_obj_t *s_hint;
static int s_last_ring = -1;
static bool s_done;

bool mn_ui_tap_is_open(void) {
    return s_page != NULL;
}

void mn_ui_tap_open(lv_obj_t *scr) {
    s_page = mn_ui_box(scr, 0, 0, 240, 320, MN_C_BG, 0);

    lv_obj_t *title = mn_ui_label(s_page, MN_FONT_BODY, MN_C_TEXT, MN_STR_TAP_TITLE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    s_ring = mn_ui_box(s_page, 120 - RING_D / 2, RING_CY - RING_D / 2, RING_D, RING_D, -1, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(s_ring, 3, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(MN_C_DOT_OFF), 0);

    s_bpm = mn_ui_label(s_page, MN_FONT_BIG, MN_C_TEXT, "--");
    lv_obj_align(s_bpm, LV_ALIGN_TOP_MID, 0, RING_CY - 66);
    s_unit = mn_ui_label(s_page, MN_FONT_SMALL, MN_C_TEXT_DIM, MN_STR_BPM);
    lv_obj_align(s_unit, LV_ALIGN_TOP_MID, 0, RING_CY + 34);

    s_count = mn_ui_label(s_page, MN_FONT_BODY, MN_C_TEXT_DIM, "");
    lv_obj_align(s_count, LV_ALIGN_TOP_MID, 0, 228);
    s_hint = mn_ui_label(s_page, MN_FONT_SMALL, MN_C_TEXT_DIM, "");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 260);
    lv_obj_t *cancel = mn_ui_label(s_page, MN_FONT_SMALL, MN_C_TEXT_FAINT, MN_STR_TAP_CANCEL);
    lv_obj_align(cancel, LV_ALIGN_TOP_MID, 0, 288);
    s_last_ring = -1;
    s_done = false;
}

void mn_ui_tap_close(void) {
    if (!s_page) return;
    lv_obj_delete(s_page);
    s_page = NULL;
}

void mn_ui_tap_update(const mn_model_t *m) {
    if (!s_page) return;
    const uint8_t taps = mn_tap_count(&m->tap);
    const uint16_t bpm = m->tap_done ? m->tap_result : mn_tap_bpm(&m->tap);
    s_done = m->tap_done;
    if (bpm) lv_label_set_text_fmt(s_bpm, "%u", (unsigned)bpm);
    else lv_label_set_text(s_bpm, "--");
    lv_obj_set_style_text_color(s_bpm, lv_color_hex(m->tap_done ? MN_C_ACCENT : MN_C_TEXT), 0);

    if (m->tap_done) {
        lv_label_set_text(s_count, MN_STR_TAP_DONE);
        lv_obj_set_style_text_color(s_count, lv_color_hex(MN_C_ACCENT), 0);
        const uint8_t term = mn_tempo_term(bpm);
        lv_label_set_text_fmt(s_hint, "%s" MN_STR_SEP "%s", MN_TEMPO_IT[term], MN_TEMPO_ZH[term]);
    } else {
        lv_obj_set_style_text_color(s_count, lv_color_hex(MN_C_TEXT_DIM), 0);
        if (taps) lv_label_set_text_fmt(s_count, MN_STR_TAP_COUNT_FMT, (unsigned)taps);
        else lv_label_set_text(s_count, MN_STR_TAP_HINT);
        if (taps == 0) lv_label_set_text(s_hint, "");
        else if (taps < MN_TAP_MIN_TAPS) lv_label_set_text(s_hint, MN_STR_TAP_NEED);
        else lv_label_set_text(s_hint, MN_STR_TAP_WAIT);
    }
    s_last_ring = -1;
}

void mn_ui_tap_ring(int flash) {
    if (!s_page || flash == s_last_ring) return;
    s_last_ring = flash;
    const uint32_t base = s_done ? MN_C_ACCENT : MN_C_DOT_OFF;
    lv_obj_set_style_border_color(s_ring, mn_ui_mix(base, MN_C_BEAT, flash), 0);
    lv_obj_set_style_border_width(s_ring, 3 + flash / 64, 0);
}
