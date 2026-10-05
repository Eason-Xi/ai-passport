// main/kj_ui_internal.h —— kj_ui.c / kj_ui_pages.c 共用的控件工具与局部刷新句柄。
#pragma once

#include "kj_fonts.h"
#include "kj_strings.h"
#include "kj_ui.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *bat_label, *bat_fill;
    int bat_shown;
    lv_obj_t *countdown_label, *countdown_bar;
    const char *countdown_fmt;
    uint8_t countdown_shown;
    lv_obj_t *clock_label;
    uint32_t clock_shown;
    uint32_t sig;
    uint8_t page;
    bool built;
} kj_ui_state_t;

extern kj_ui_state_t kj_ui;

enum {
    KJ_CARD_SEL = 1u << 0,    // 选中（上浮 + 红框）
    KJ_CARD_OFF = 1u << 1,    // 用完 / 不可选（变暗）
    KJ_CARD_BACK = 1u << 2,   // 背面（已扣下的暗牌）
    KJ_CARD_BIG = 1u << 3,    // 64 px 手势
    KJ_CARD_COUNT = 1u << 4,  // 底部显示 ×N
    KJ_CARD_NAME = 1u << 5,   // 底部显示牌名
};

lv_obj_t *kj_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius);
lv_obj_t *kj_frame(lv_obj_t *parent, int x, int y, int w, int h, uint32_t border, int width, int radius);
lv_obj_t *kj_text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s);
lv_obj_t *kj_text_at(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s,
                     lv_align_t align, int x, int y);
lv_obj_t *kj_text_box(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *s,
                      int x, int y, int w, lv_text_align_t align);
lv_obj_t *kj_card(lv_obj_t *parent, int x, int y, int w, int h, uint8_t card, int count, uint32_t flags);
lv_obj_t *kj_badge(lv_obj_t *parent, int cx, int cy, int d, unsigned no, uint32_t ring, bool big);
lv_obj_t *kj_pill(lv_obj_t *parent, int x, int y, int w, int h, const char *s, bool filled, uint32_t color);
void kj_stars(lv_obj_t *parent, int y, unsigned stars, const lv_font_t *font);
void kj_footer(lv_obj_t *scr, const char *s);
void kj_top_bar(lv_obj_t *scr, const char *left, uint32_t left_color);
void kj_pulse(lv_obj_t *obj, uint32_t period_ms);
void kj_set_text_opa(lv_obj_t *obj, int32_t v);
const char *kj_card_name(uint8_t card);
const char *kj_card_glyph(uint8_t card);

// kj_ui_pages.c
void kj_ui_build_page(const kj_ui_model_t *m);
