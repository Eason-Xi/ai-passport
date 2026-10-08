// main/as_ui_pages.c —— 调音台、选声音、睡眠定时与呼吸引导页。
#include <stdio.h>

#include "as_breath.h"
#include "as_fonts.h"
#include "as_strings.h"
#include "as_theme.h"
#include "as_ui_internal.h"

// ---- 调音台 ----

#define CARD_X 12
#define CARD_Y0 42
#define CARD_STEP 66
#define CARD_W (AS_SCREEN_W - 24)
#define CARD_H 60

static lv_obj_t *s_cards[AS_LAYERS];
static lv_obj_t *s_card_dot[AS_LAYERS], *s_card_name[AS_LAYERS], *s_card_level[AS_LAYERS];
static lv_obj_t *s_card_bar[AS_LAYERS];
static lv_obj_t *s_done, *s_done_text, *s_hint;

// ---- 选声音 ----

#define TILE_W 104
#define TILE_H 34

static lv_obj_t *s_tiles[AS_PICK_COUNT];
static lv_obj_t *s_desc;

// ---- 睡眠定时 ----

static lv_obj_t *s_wheel_prev, *s_wheel_next, *s_wheel_num, *s_wheel_unit, *s_wheel_off, *s_timer_info;

// ---- 呼吸引导 ----

#define BREATH_CX 120
#define BREATH_CY 150
#define BREATH_MIN 72
#define BREATH_MAX 192

static lv_obj_t *s_breath_title, *s_breath_desc, *s_breath_ball, *s_breath_count, *s_breath_phase;
static uint8_t s_breath_pattern;
static uint32_t s_breath_start;
static int s_breath_d = -1;
static int s_breath_remain = -1;
static int s_breath_ph = -1;

void as_ui_pages_forget(void) {
    for (int i = 0; i < AS_LAYERS; i++) {
        s_cards[i] = s_card_dot[i] = s_card_name[i] = s_card_level[i] = s_card_bar[i] = NULL;
    }
    s_done = s_done_text = s_hint = NULL;
    for (int i = 0; i < AS_PICK_COUNT; i++) s_tiles[i] = NULL;
    s_desc = NULL;
    s_wheel_prev = s_wheel_next = s_wheel_num = s_wheel_unit = s_wheel_off = s_timer_info = NULL;
    s_breath_title = s_breath_desc = s_breath_ball = s_breath_count = s_breath_phase = NULL;
    s_breath_d = s_breath_remain = s_breath_ph = -1;
}

static lv_obj_t *title(lv_obj_t *root, const char *text) {
    lv_obj_t *t = as_ui_label(root, AS_FONT_BODY, AS_C_TEXT, text);
    lv_obj_set_pos(t, 16, 10);
    return t;
}

static lv_obj_t *hint(lv_obj_t *root, const char *text) {
    return as_ui_label_center(root, AS_FONT_SMALL, AS_C_TEXT_FAINT, text, 0, 294, AS_SCREEN_W);
}

void as_ui_mixer_build(lv_obj_t *root, const as_model_t *m) {
    title(root, AS_STR_MIXER_TITLE);
    for (int i = 0; i < AS_LAYERS; i++) {
        lv_obj_t *card = as_ui_box(root, CARD_X, CARD_Y0 + i * CARD_STEP, CARD_W, CARD_H, AS_C_PANEL, 14);
        lv_obj_set_style_border_width(card, 2, 0);
        s_cards[i] = card;
        s_card_dot[i] = as_ui_circle(card, 19, 17, 10, AS_C_SOUND_NONE);
        char buf[16];
        snprintf(buf, sizeof buf, AS_STR_TRACK_FMT, i + 1);
        lv_obj_t *track = as_ui_label(card, AS_FONT_SMALL, AS_C_TEXT_DIM, buf);
        lv_obj_set_pos(track, 30, 6);
        s_card_level[i] = as_ui_label(card, AS_FONT_BODY, AS_C_TEXT_DIM, "");
        lv_obj_set_style_text_align(s_card_level[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(s_card_level[i], 40);
        lv_obj_set_pos(s_card_level[i], CARD_W - 54, 3);
        s_card_name[i] = as_ui_label(card, AS_FONT_BODY, AS_C_TEXT, "");
        lv_obj_set_pos(s_card_name[i], 12, 28);
        s_card_bar[i] = as_ui_segments(card, 100, 36, 8, 12, 2);
    }
    s_done = as_ui_box(root, 60, 244, 120, 34, AS_C_PANEL, 17);
    s_done_text = as_ui_label_center(s_done, AS_FONT_BODY, AS_C_TEXT, AS_STR_DONE, 0, 5, 120);
    s_hint = hint(root, "");
    as_ui_mixer_update(m);
}

void as_ui_mixer_update(const as_model_t *m) {
    if (!s_cards[0]) return;
    for (int i = 0; i < AS_LAYERS; i++) {
        const uint8_t sound = m->cfg.sound[i];
        const uint8_t level = m->cfg.level[i];
        const uint32_t col = as_sound_color(sound);
        const bool focus = m->mix_sel == i;
        const bool edit = focus && m->mix_edit;
        lv_obj_set_style_bg_color(s_cards[i], lv_color_hex(edit ? AS_C_PANEL_HI : AS_C_PANEL), 0);
        lv_obj_set_style_border_color(s_cards[i], lv_color_hex(edit ? col : AS_C_MOON), 0);
        lv_obj_set_style_border_opa(s_cards[i], focus ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_card_dot[i], lv_color_hex(col), 0);
        lv_label_set_text(s_card_name[i], as_ui_sound_name(sound));
        lv_obj_set_style_text_color(s_card_name[i], lv_color_hex(sound == AS_SOUND_NONE ? AS_C_TEXT_FAINT : AS_C_TEXT),
                                    0);
        if (sound == AS_SOUND_NONE) {
            lv_label_set_text(s_card_level[i], "");
            lv_obj_add_flag(s_card_bar[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text_fmt(s_card_level[i], "%u", (unsigned)level);
            lv_obj_set_style_text_color(s_card_level[i], lv_color_hex(edit ? AS_C_MOON : AS_C_TEXT_DIM), 0);
            lv_obj_remove_flag(s_card_bar[i], LV_OBJ_FLAG_HIDDEN);
            as_ui_segments_set(s_card_bar[i], level, col);
        }
    }
    const bool done = m->mix_sel == AS_MIX_DONE;
    lv_obj_set_style_bg_color(s_done, lv_color_hex(done ? AS_C_MOON : AS_C_PANEL), 0);
    lv_obj_set_style_text_color(s_done_text, lv_color_hex(done ? AS_C_MOON_DEEP : AS_C_TEXT), 0);
    lv_label_set_text(s_hint, m->mix_edit ? AS_STR_MIXER_EDIT_HINT : AS_STR_MIXER_HINT);
}

void as_ui_picker_build(lv_obj_t *root, const as_model_t *m) {
    char buf[48];
    snprintf(buf, sizeof buf, AS_STR_PICKER_TITLE_FMT, m->mix_sel + 1);
    title(root, buf);
    for (int i = 0; i < AS_PICK_COUNT; i++) {
        const uint8_t sound = as_pick_to_sound((uint8_t)i);
        lv_obj_t *tile = as_ui_box(root, 12 + (i % 2) * (TILE_W + 8), 42 + (i / 2) * 38, TILE_W, TILE_H, AS_C_PANEL,
                                   10);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(as_sound_color(sound)), 0);
        s_tiles[i] = tile;
        as_ui_circle(tile, 15, 17, 8, (int32_t)as_sound_color(sound));
        lv_obj_t *name = as_ui_label(tile, AS_FONT_BODY, sound == AS_SOUND_NONE ? AS_C_TEXT_DIM : AS_C_TEXT,
                                     as_ui_sound_name(sound));
        lv_obj_set_pos(name, 27, 5);
    }
    s_desc = as_ui_label_center(root, AS_FONT_SMALL, AS_C_TEXT_DIM, "", 0, 274, AS_SCREEN_W);
    hint(root, AS_STR_PICKER_HINT);
    as_ui_picker_update(m);
}

void as_ui_picker_update(const as_model_t *m) {
    if (!s_tiles[0]) return;
    for (int i = 0; i < AS_PICK_COUNT; i++) {
        const bool on = i == m->pick_sel;
        lv_obj_set_style_bg_color(s_tiles[i], lv_color_hex(on ? AS_C_PANEL_HI : AS_C_PANEL), 0);
        lv_obj_set_style_border_opa(s_tiles[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    const uint8_t sound = as_pick_to_sound(m->pick_sel);
    lv_label_set_text(s_desc, sound < AS_SOUND_COUNT ? AS_STR_SOUND_DESC[sound] : AS_STR_SOUND_NONE_DESC);
}

// ---- 睡眠定时 ----

static void option_text(char *buf, size_t n, int opt) {
    if (opt == AS_TIMER_OFF) snprintf(buf, n, "%s", AS_STR_NO_TIMER);
    else snprintf(buf, n, AS_STR_MINUTES_FMT, (unsigned)as_timer_minutes((uint8_t)opt));
}

void as_ui_timer_build(lv_obj_t *root, const as_model_t *m) {
    as_ui_moon(root, 16, 13, 16);
    lv_obj_t *t = title(root, AS_STR_TIMER_TITLE);
    lv_obj_set_x(t, 40);
    s_wheel_prev = as_ui_label_center(root, AS_FONT_BODY, AS_C_TEXT_FAINT, "", 0, 70, AS_SCREEN_W);
    lv_obj_t *pill = as_ui_box(root, 20, 104, 200, 88, AS_C_PANEL_HI, 22);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(AS_C_MOON), 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_50, 0);
    s_wheel_num = as_ui_label_center(pill, AS_FONT_BIG, AS_C_MOON, "", 0, 2, 200);
    s_wheel_unit = as_ui_label_center(pill, AS_FONT_SMALL, AS_C_TEXT_DIM, AS_STR_MINUTES, 0, 60, 200);
    s_wheel_off = as_ui_label_center(pill, AS_FONT_TITLE, AS_C_MOON, AS_STR_NO_TIMER, 0, 22, 200);
    s_wheel_next = as_ui_label_center(root, AS_FONT_BODY, AS_C_TEXT_FAINT, "", 0, 204, AS_SCREEN_W);
    s_timer_info = as_ui_label_center(root, AS_FONT_SMALL, AS_C_TEXT_DIM, "", 0, 246, AS_SCREEN_W);
    hint(root, AS_STR_TIMER_HINT);
    as_ui_timer_update(m);
}

void as_ui_timer_update(const as_model_t *m) {
    if (!s_wheel_num) return;
    char buf[24];
    const int sel = m->timer_sel;
    if (sel > 0) option_text(buf, sizeof buf, sel - 1);
    else buf[0] = '\0';
    lv_label_set_text(s_wheel_prev, buf);
    if (sel + 1 < AS_TIMER_COUNT) option_text(buf, sizeof buf, sel + 1);
    else buf[0] = '\0';
    lv_label_set_text(s_wheel_next, buf);
    const bool off = sel == AS_TIMER_OFF;
    if (off) {
        lv_obj_add_flag(s_wheel_num, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wheel_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_wheel_off, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text_fmt(s_wheel_num, "%u", (unsigned)as_timer_minutes((uint8_t)sel));
        lv_obj_remove_flag(s_wheel_num, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_wheel_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wheel_off, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_timer_info, off ? AS_STR_TIMER_OFF_INFO : AS_STR_TIMER_ON_INFO);
}

// ---- 呼吸引导 ----

void as_ui_breath_build(lv_obj_t *root, const as_model_t *m) {
    s_breath_title = as_ui_label_center(root, AS_FONT_BODY, AS_C_TEXT, "", 0, 10, AS_SCREEN_W);
    s_breath_desc = as_ui_label_center(root, AS_FONT_SMALL, AS_C_TEXT_DIM, "", 0, 34, AS_SCREEN_W);
    lv_obj_t *guide = as_ui_circle(root, BREATH_CX, BREATH_CY, BREATH_MAX + 6, -1);
    lv_obj_set_style_border_width(guide, 1, 0);
    lv_obj_set_style_border_color(guide, lv_color_hex(AS_C_BREATH), 0);
    lv_obj_set_style_border_opa(guide, LV_OPA_40, 0);
    s_breath_ball = as_ui_circle(root, BREATH_CX, BREATH_CY, BREATH_MIN, AS_C_BREATH);
    lv_obj_set_style_bg_opa(s_breath_ball, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_breath_ball, 2, 0);
    lv_obj_set_style_border_color(s_breath_ball, lv_color_hex(AS_C_BREATH), 0);
    s_breath_count = as_ui_label_center(root, AS_FONT_BIG, AS_C_TEXT, "", BREATH_CX - 50, BREATH_CY - 30, 100);
    s_breath_phase = as_ui_label_center(root, AS_FONT_TITLE, AS_C_BREATH, "", 0, 252, AS_SCREEN_W);
    hint(root, AS_STR_BREATH_HINT);
    as_ui_breath_update(m);
}

void as_ui_breath_update(const as_model_t *m) {
    if (!s_breath_ball) return;
    s_breath_pattern = m->cfg.breath < AS_BREATH_COUNT ? m->cfg.breath : 0;
    s_breath_start = m->breath_start_ms;
    lv_label_set_text(s_breath_title, AS_STR_BREATH_NAME[s_breath_pattern]);
    lv_label_set_text(s_breath_desc, AS_STR_BREATH_DESC[s_breath_pattern]);
    s_breath_d = s_breath_remain = s_breath_ph = -1;
    as_ui_breath_animate(m->breath_start_ms);
}

void as_ui_breath_animate(uint32_t now_ms) {
    if (!s_breath_ball) return;
    as_breath_t b;
    as_breath_at(s_breath_pattern, now_ms - s_breath_start, &b);
    const int d = BREATH_MIN + (int)((int64_t)(BREATH_MAX - BREATH_MIN) * b.size / 32767);
    if (d != s_breath_d) {
        s_breath_d = d;
        as_ui_circle_move(s_breath_ball, BREATH_CX, BREATH_CY, d);
    }
    if (b.remain_s != s_breath_remain) {
        s_breath_remain = b.remain_s;
        lv_label_set_text_fmt(s_breath_count, "%u", (unsigned)b.remain_s);
    }
    if (b.phase != s_breath_ph) {
        s_breath_ph = b.phase;
        lv_label_set_text(s_breath_phase, AS_STR_PHASE[b.phase]);
    }
}
