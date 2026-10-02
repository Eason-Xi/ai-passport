// main/yz_ui_library.c —— 字帖目录：上方是所选字帖的题签字（碑石）、竖排帖名、年代与进度，
// 下方是全部字帖的列表；当前打开的字帖标“当前”。
#include "yz_ui_internal.h"

#define TABLET 96
#define ROW_Y 200
#define ROW_H 27

// 列表区最多放三行。
_Static_assert(YZ_BOOK_COUNT <= 3, "library list fits at most three books");

static lv_obj_t *s_title;
static lv_obj_t *s_meta;
static lv_obj_t *s_done;
static lv_obj_t *s_rows[YZ_BOOK_COUNT];
static lv_obj_t *s_marks[YZ_BOOK_COUNT];
static lv_obj_t *s_names[YZ_BOOK_COUNT];
static lv_obj_t *s_tags[YZ_BOOK_COUNT];

static void update(const yz_book_t *book) {
    const yz_book_info_t *info = &YZ_BOOKS[book->lib_sel];
    lv_label_set_text_static(s_title, info->name_v);
    lv_label_set_text_fmt(s_meta, YZ_STR_LIB_META_FMT, info->era, (unsigned)info->entry_count);
    lv_label_set_text_fmt(s_done, YZ_STR_LIB_DONE_FMT, (unsigned)yz_book_done_count(book, book->lib_sel));
    for (int b = 0; b < YZ_BOOK_COUNT; b++) {
        const bool sel = b == book->lib_sel;
        lv_obj_set_style_bg_opa(s_rows[b], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_names[b], lv_color_hex(sel ? YZ_C_VERMILION : YZ_C_INK), 0);
        if (sel) lv_obj_remove_flag(s_marks[b], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_marks[b], LV_OBJ_FLAG_HIDDEN);
        if (b == book->prog.book) lv_obj_remove_flag(s_tags[b], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_tags[b], LV_OBJ_FLAG_HIDDEN);
    }
}

static bool build(lv_obj_t *scr, const yz_book_t *book) {
    yz_ui_box(scr, 22, 14, 4, 22, YZ_C_VERMILION, 1);
    lv_obj_t *head = yz_ui_label(scr, &yz_zh24, YZ_C_INK, 34, 10);
    lv_label_set_text_static(head, YZ_STR_LIB_TITLE);

    lv_obj_t *tablet = yz_ui_box(scr, 22, 46, TABLET, TABLET, YZ_C_STONE, 4);
    lv_obj_t *img = yz_ui_glyph_image(tablet, 128, YZ_C_STONE_TEXT);   // 176 × 0.5 = 88 px
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    s_title = yz_ui_label(scr, &yz_zh32, YZ_C_INK, 146, 36);
    lv_obj_set_style_text_line_space(s_title, -4, 0);

    s_meta = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 22, 158);
    s_done = yz_ui_label(scr, &yz_zh14, YZ_C_VERMILION, 22, 176);

    for (int b = 0; b < YZ_BOOK_COUNT; b++) {
        s_rows[b] = yz_ui_box(scr, 18, ROW_Y + b * ROW_H, 204, ROW_H - 2, YZ_C_PAPER_DEEP, 5);
        s_marks[b] = yz_ui_box(s_rows[b], 0, 6, 3, ROW_H - 14, YZ_C_VERMILION, 1);
        s_names[b] = yz_ui_label(s_rows[b], &yz_zh18, YZ_C_INK, 12, -1);
        lv_label_set_text_static(s_names[b], YZ_BOOKS[b].name);
        s_tags[b] = yz_ui_label(s_rows[b], &yz_zh14, YZ_C_PAPER, 0, 0);
        lv_obj_set_style_bg_color(s_tags[b], lv_color_hex(YZ_C_INK_SOFT), 0);
        lv_obj_set_style_bg_opa(s_tags[b], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_tags[b], 3, 0);
        lv_obj_set_style_pad_hor(s_tags[b], 5, 0);
        lv_label_set_text_static(s_tags[b], YZ_STR_LIB_OPEN);
        lv_obj_align(s_tags[b], LV_ALIGN_RIGHT_MID, -8, 0);
    }

    lv_obj_t *hint = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 0, 0);
    lv_label_set_text_static(hint, YZ_STR_LIB_HINT);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);

    update(book);
    return false;
}

static void forget(void) {
    s_title = s_meta = s_done = NULL;
    for (int b = 0; b < YZ_BOOK_COUNT; b++) s_rows[b] = s_marks[b] = s_names[b] = s_tags[b] = NULL;
}

const yz_page_t YZ_PAGE_LIBRARY = { build, update, forget };
