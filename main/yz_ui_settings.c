// main/yz_ui_settings.c —— 设置：七行，上下选择、确定切换取值；清除进度需连按两次确定。
#include "yz_ui_internal.h"

#define ROW_Y 50
#define ROW_H 32

static const char *const LABELS[YZ_SET_COUNT] = {
    YZ_STR_SET_GRID, YZ_STR_SET_INK, YZ_STR_SET_TIMER, YZ_STR_SET_AUTO,
    YZ_STR_SET_SOUND, YZ_STR_SET_BRIGHT, YZ_STR_SET_RESET,
};

static lv_obj_t *s_rows[YZ_SET_COUNT];
static lv_obj_t *s_marks[YZ_SET_COUNT];
static lv_obj_t *s_names[YZ_SET_COUNT];
static lv_obj_t *s_values[YZ_SET_COUNT];

static void update(const yz_book_t *book) {
    const yz_cfg_t *c = &book->cfg;
    for (int i = 0; i < YZ_SET_COUNT; i++) {
        const bool sel = i == book->set_sel;
        lv_obj_set_style_bg_opa(s_rows[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (sel) lv_obj_remove_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_values[i], lv_color_hex(sel ? YZ_C_VERMILION : YZ_C_INK_SOFT), 0);
    }
    lv_label_set_text_static(s_values[YZ_SET_GRID], yz_ui_grid_name(c->grid));
    lv_label_set_text_static(s_values[YZ_SET_INK], yz_ui_ink_name(c->ink));
    lv_label_set_text_fmt(s_values[YZ_SET_TIMER], YZ_STR_SECONDS_FMT, (unsigned)YZ_TIMER_SECONDS[c->timer_idx]);
    lv_label_set_text_static(s_values[YZ_SET_AUTO], c->auto_next ? YZ_STR_ON : YZ_STR_OFF);
    lv_label_set_text_static(s_values[YZ_SET_SOUND], c->sound ? YZ_STR_ON : YZ_STR_OFF);
    lv_label_set_text_fmt(s_values[YZ_SET_BRIGHT], YZ_STR_PERCENT_FMT, (unsigned)YZ_BRIGHT_PERCENT[c->bright_idx]);
    lv_label_set_text_static(s_values[YZ_SET_RESET], book->confirm_reset ? YZ_STR_RESET_CONFIRM : YZ_STR_RESET_ASK);
}

static bool build(lv_obj_t *scr, const yz_book_t *book) {
    yz_ui_box(scr, 22, 14, 4, 22, YZ_C_VERMILION, 1);
    lv_obj_t *title = yz_ui_label(scr, &yz_zh24, YZ_C_INK, 34, 10);
    lv_label_set_text_static(title, YZ_STR_SET_TITLE);

    for (int i = 0; i < YZ_SET_COUNT; i++) {
        s_rows[i] = yz_ui_box(scr, 18, ROW_Y + i * ROW_H, 204, ROW_H - 4, YZ_C_PAPER_DEEP, 5);
        s_marks[i] = yz_ui_box(s_rows[i], 0, 6, 3, ROW_H - 16, YZ_C_VERMILION, 1);
        s_names[i] = yz_ui_label(s_rows[i], &yz_zh18, YZ_C_INK, 12, 2);
        lv_label_set_text_static(s_names[i], LABELS[i]);
        s_values[i] = yz_ui_label(s_rows[i], &yz_zh18, YZ_C_INK_SOFT, 100, 2);
        lv_obj_set_width(s_values[i], 94);
        lv_obj_set_style_text_align(s_values[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_t *hint = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 0, 0);
    lv_label_set_text_static(hint, YZ_STR_SET_HINT);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);

    update(book);
    return false;
}

static void forget(void) {
    for (int i = 0; i < YZ_SET_COUNT; i++) s_rows[i] = s_marks[i] = s_names[i] = s_values[i] = NULL;
}

const yz_page_t YZ_PAGE_SETTINGS = { build, update, forget };
