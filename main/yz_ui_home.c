// main/yz_ui_home.c —— 主页：碑石上的“当前字”、竖排帖名与朱印、本帖统计、六项菜单。
#include "yz_ui_internal.h"

#define MENU_Y 180
#define MENU_ROW 22
#define TABLET 112

static lv_obj_t *s_rows[YZ_HOME_COUNT];
static lv_obj_t *s_names[YZ_HOME_COUNT];
static lv_obj_t *s_marks[YZ_HOME_COUNT];
static lv_obj_t *s_extra_cont;
static lv_obj_t *s_extra_fav;
static lv_obj_t *s_extra_lib;
static lv_obj_t *s_stats;

static const char *const ITEMS[YZ_HOME_COUNT] = {
    YZ_STR_HOME_CONTINUE, YZ_STR_HOME_CATALOG, YZ_STR_HOME_FAVORITES,
    YZ_STR_HOME_LIBRARY, YZ_STR_HOME_ABOUT, YZ_STR_HOME_SETTINGS,
};
static lv_obj_t *s_title;

static void update(const yz_book_t *book) {
    for (int i = 0; i < YZ_HOME_COUNT; i++) {
        const bool sel = i == book->home_sel;
        lv_obj_set_style_bg_opa(s_rows[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_names[i], lv_color_hex(sel ? YZ_C_VERMILION : YZ_C_INK), 0);
        if (sel) lv_obj_remove_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
    }
    const int b = book->prog.book;
    const yz_book_info_t *info = &YZ_BOOKS[b];
    const int cur = book->prog.current[b];
    lv_label_set_text_static(s_title, info->name_v);
    lv_label_set_text_fmt(s_extra_cont, YZ_STR_HOME_CONT_FMT, YZ_ENTRIES[cur].simp,
                          (unsigned)(cur - info->first_entry + 1), (unsigned)info->entry_count);
    lv_label_set_text_fmt(s_extra_fav, YZ_STR_HOME_FAV_FMT, (unsigned)yz_book_fav_count(book, b));
    lv_label_set_text_fmt(s_extra_lib, YZ_STR_HOME_LIB_FMT, (unsigned)YZ_BOOK_COUNT);
    lv_label_set_text_fmt(s_stats, YZ_STR_HOME_STATS_FMT, (unsigned long)book->prog.sessions[b],
                          (unsigned long)(book->prog.seconds[b] / 60u));
}

static bool build(lv_obj_t *scr, const yz_book_t *book) {
    lv_obj_t *author = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 24, 11);
    lv_label_set_text_static(author, YZ_BOOKS[book->prog.book].author);

    // 碑石：显示上次临写的字。
    lv_obj_t *tablet = yz_ui_box(scr, 22, 34, TABLET, TABLET, YZ_C_STONE, 4);
    lv_obj_t *img = yz_ui_glyph_image(tablet, 151, YZ_C_STONE_TEXT);   // 176 × 0.59 ≈ 104 px
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    s_title = yz_ui_label(scr, &yz_zh32, YZ_C_INK, 152, 26);
    lv_obj_set_style_text_line_space(s_title, -4, 0);

    lv_obj_t *seal = yz_ui_box(scr, 197, 104, 22, 42, YZ_C_VERMILION, 3);
    lv_obj_t *seal_text = yz_ui_label(seal, &yz_zh14, YZ_C_PAPER, 4, 1);
    lv_obj_set_style_text_line_space(seal_text, -2, 0);
    lv_obj_set_width(seal_text, 14);
    lv_label_set_long_mode(seal_text, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text_static(seal_text, YZ_STR_SEAL);

    s_stats = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 22, 152);

    lv_obj_t *rule = yz_ui_box(scr, 22, MENU_Y - 6, 196, 1, YZ_C_INK_SOFT, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);

    for (int i = 0; i < YZ_HOME_COUNT; i++) {
        s_rows[i] = yz_ui_box(scr, 18, MENU_Y + i * MENU_ROW, 204, MENU_ROW - 1, YZ_C_PAPER_DEEP, 4);
        s_marks[i] = yz_ui_box(s_rows[i], 0, 4, 3, MENU_ROW - 9, YZ_C_VERMILION, 1);
        s_names[i] = yz_ui_label(s_rows[i], &yz_zh18, YZ_C_INK, 10, -1);
        lv_label_set_text_static(s_names[i], ITEMS[i]);
    }
    s_extra_cont = yz_ui_label(s_rows[YZ_HOME_CONTINUE], &yz_zh14, YZ_C_INK_SOFT, 96, 2);
    lv_obj_set_width(s_extra_cont, 100);
    lv_obj_set_style_text_align(s_extra_cont, LV_TEXT_ALIGN_RIGHT, 0);
    s_extra_fav = yz_ui_label(s_rows[YZ_HOME_FAVORITES], &yz_zh14, YZ_C_INK_SOFT, 96, 2);
    lv_obj_set_width(s_extra_fav, 100);
    lv_obj_set_style_text_align(s_extra_fav, LV_TEXT_ALIGN_RIGHT, 0);
    s_extra_lib = yz_ui_label(s_rows[YZ_HOME_LIBRARY], &yz_zh14, YZ_C_INK_SOFT, 96, 2);
    lv_obj_set_width(s_extra_lib, 100);
    lv_obj_set_style_text_align(s_extra_lib, LV_TEXT_ALIGN_RIGHT, 0);

    update(book);
    return false;
}

static void forget(void) {
    for (int i = 0; i < YZ_HOME_COUNT; i++) s_rows[i] = s_names[i] = s_marks[i] = NULL;
    s_extra_cont = s_extra_fav = s_extra_lib = s_stats = s_title = NULL;
}

const yz_page_t YZ_PAGE_HOME = { build, update, forget };
