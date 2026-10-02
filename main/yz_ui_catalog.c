// main/yz_ui_catalog.c —— 目录：章节页签、5×4 字格（碑上原字）、底部拓本预览与释文。
#include "yz_ui_internal.h"

#define TAB_Y 36
#define TAB_X 12
#define TAB_W 31
#define GRID_X 20
#define GRID_Y 62
#define CELL 40
#define CELLS (YZ_CATALOG_COLS * YZ_CATALOG_ROWS)
#define PREVIEW_Y 230

static lv_obj_t *s_head;
static lv_obj_t *s_tabs[YZ_TAB_COUNT];
static lv_obj_t *s_tab_line;
static lv_obj_t *s_cells[CELLS];
static lv_obj_t *s_cell_text[CELLS];
static lv_obj_t *s_cell_fav[CELLS];
static lv_obj_t *s_cell_done[CELLS];
static lv_obj_t *s_scroll;
static lv_obj_t *s_empty;
static lv_obj_t *s_tile;
static lv_obj_t *s_simp;
static lv_obj_t *s_pinyin;
static lv_obj_t *s_phrase;
static lv_obj_t *s_count;

static void update(const yz_book_t *book) {
    lv_label_set_text_fmt(s_head, YZ_STR_CAT_HEAD_FMT, yz_ui_tab_name(book->tab), (unsigned)book->list_len);
    for (int t = 0; t < YZ_TAB_COUNT; t++) {
        const bool cur = t == book->tab;
        lv_obj_set_style_text_color(s_tabs[t], lv_color_hex(cur ? YZ_C_VERMILION : YZ_C_INK_SOFT), 0);
        lv_obj_set_style_text_opa(s_tabs[t], cur ? LV_OPA_COVER : LV_OPA_60, 0);
    }
    lv_obj_set_x(s_tab_line, TAB_X + book->tab * TAB_W + 3);

    const bool empty = book->list_len == 0;
    for (int c = 0; c < CELLS; c++) {
        const int idx = (book->first_row * YZ_CATALOG_COLS) + c;
        if (idx >= book->list_len) {
            lv_obj_add_flag(s_cells[c], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int entry = book->list[idx];
        const bool sel = idx == book->pos;
        lv_obj_remove_flag(s_cells[c], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(s_cells[c], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_cell_text[c], lv_color_hex(sel ? YZ_C_PAPER : YZ_C_INK), 0);
        lv_label_set_text_static(s_cell_text[c], YZ_ENTRIES[entry].trad);
        if (yz_book_is_fav(book, entry)) lv_obj_remove_flag(s_cell_fav[c], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_cell_fav[c], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_cell_fav[c], lv_color_hex(sel ? YZ_C_PAPER : YZ_C_VERMILION), 0);
        if (book->prog.count[entry]) lv_obj_remove_flag(s_cell_done[c], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_cell_done[c], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_cell_done[c], lv_color_hex(sel ? YZ_C_PAPER : YZ_C_INK_SOFT), 0);
    }

    // 滚动条：总行数超过可见行数时显示。
    const int rows = (book->list_len + YZ_CATALOG_COLS - 1) / YZ_CATALOG_COLS;
    if (rows > YZ_CATALOG_ROWS) {
        const int track = CELL * YZ_CATALOG_ROWS;
        const int h = track * YZ_CATALOG_ROWS / rows;
        lv_obj_remove_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(s_scroll, h);
        lv_obj_set_y(s_scroll, GRID_Y + (track - h) * book->first_row / (rows - YZ_CATALOG_ROWS));
    } else {
        lv_obj_add_flag(s_scroll, LV_OBJ_FLAG_HIDDEN);
    }

    if (empty) {
        lv_obj_remove_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_tile, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_static(s_simp, "");
        lv_label_set_text_static(s_pinyin, "");
        lv_label_set_text_static(s_phrase, "");
        lv_label_set_text_static(s_count, "");
        return;
    }
    lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_tile, LV_OBJ_FLAG_HIDDEN);
    const int entry = yz_book_entry(book);
    const yz_entry_t *e = &YZ_ENTRIES[entry];
    lv_label_set_text_static(s_simp, e->simp);
    lv_label_set_text_static(s_pinyin, e->pinyin);
    lv_label_set_text_fmt(s_phrase, YZ_STR_PREVIEW_PHRASE_FMT, e->phrase);
    if (book->prog.count[entry]) lv_label_set_text_fmt(s_count, YZ_STR_COUNT_FMT, (unsigned)book->prog.count[entry]);
    else lv_label_set_text_static(s_count, YZ_STR_COUNT_NONE);
}

static bool build(lv_obj_t *scr, const yz_book_t *book) {
    s_head = yz_ui_label(scr, &yz_zh18, YZ_C_INK, 24, 8);

    for (int t = 0; t < YZ_TAB_COUNT; t++) {
        s_tabs[t] = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, TAB_X + t * TAB_W, TAB_Y);
        lv_label_set_text_static(s_tabs[t], yz_ui_tab_name((uint8_t)t));
    }
    s_tab_line = yz_ui_box(scr, TAB_X + 3, TAB_Y + 20, 22, 2, YZ_C_VERMILION, 1);

    for (int c = 0; c < CELLS; c++) {
        const int x = GRID_X + (c % YZ_CATALOG_COLS) * CELL;
        const int y = GRID_Y + (c / YZ_CATALOG_COLS) * CELL;
        s_cells[c] = yz_ui_box(scr, x + 2, y + 2, CELL - 4, CELL - 4, YZ_C_VERMILION, 5);
        s_cell_text[c] = yz_ui_label(s_cells[c], &yz_zh24, YZ_C_INK, 0, 0);
        lv_obj_align(s_cell_text[c], LV_ALIGN_CENTER, 0, -1);
        s_cell_fav[c] = yz_ui_box(s_cells[c], CELL - 10, 2, 4, 4, YZ_C_VERMILION, 2);
        s_cell_done[c] = yz_ui_box(s_cells[c], CELL - 10, CELL - 10, 4, 4, YZ_C_INK_SOFT, 2);
    }
    s_scroll = yz_ui_box(scr, 223, GRID_Y, 3, CELL * YZ_CATALOG_ROWS, YZ_C_INK_SOFT, 1);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_50, 0);

    s_empty = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 0, 0);
    lv_obj_set_width(s_empty, 200);
    lv_obj_set_style_text_align(s_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_empty, 6, 0);
    lv_label_set_text_static(s_empty, YZ_STR_CAT_EMPTY);
    lv_obj_align(s_empty, LV_ALIGN_TOP_MID, 0, GRID_Y + 60);

    // 底部预览：缩小的拓本 + 简体、拼音、碑文语境、临写遍数。
    s_tile = yz_ui_box(scr, 20, PREVIEW_Y, 76, 76, YZ_C_STONE, 4);
    lv_obj_t *img = yz_ui_glyph_image(s_tile, 108, YZ_C_STONE_TEXT);   // 176 × 0.42 ≈ 74 px
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    s_simp = yz_ui_label(scr, &yz_zh32, YZ_C_INK, 106, PREVIEW_Y - 4);
    s_pinyin = yz_ui_label(scr, &yz_zh18, YZ_C_VERMILION, 146, PREVIEW_Y + 4);
    s_phrase = yz_ui_label(scr, &yz_zh14, YZ_C_INK, 106, PREVIEW_Y + 38);
    lv_obj_set_width(s_phrase, 120);
    lv_label_set_long_mode(s_phrase, LV_LABEL_LONG_MODE_CLIP);
    s_count = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 106, PREVIEW_Y + 58);

    update(book);
    return false;
}

static void forget(void) {
    s_head = s_tab_line = s_scroll = s_empty = s_tile = NULL;
    s_simp = s_pinyin = s_phrase = s_count = NULL;
    for (int t = 0; t < YZ_TAB_COUNT; t++) s_tabs[t] = NULL;
    for (int c = 0; c < CELLS; c++) s_cells[c] = s_cell_text[c] = s_cell_fav[c] = s_cell_done[c] = NULL;
}

const yz_page_t YZ_PAGE_CATALOG = { build, update, forget };
