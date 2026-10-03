// main/yz_ui_volumes.c —— 分卷目录：全碑临写进度，以及九个碑文卷、五个分类卷的列表
// （卷名、进度条、已临 / 字数）。一屏显示九行，选中行超出时整页滚动。
#include "yz_ui_internal.h"

#define LIST_Y 66
#define ROW_H 23
#define ROWS 9
#define BAR_X 52
#define BAR_W 80

static lv_obj_t *s_total;
static lv_obj_t *s_rows[ROWS];
static lv_obj_t *s_marks[ROWS];
static lv_obj_t *s_names[ROWS];
static lv_obj_t *s_tracks[ROWS];
static lv_obj_t *s_fills[ROWS];
static lv_obj_t *s_counts[ROWS];
static lv_obj_t *s_scroll;

// 可见窗口的第一行：选中行尽量留在窗口里，靠近两端时贴边。
static int first_row(int sel) {
    int first = sel - ROWS / 2;
    if (first > YZ_CHAPTER_COUNT - ROWS) first = YZ_CHAPTER_COUNT - ROWS;
    return first < 0 ? 0 : first;
}

static void update(const yz_book_t *book) {
    lv_label_set_text_fmt(s_total, YZ_STR_VOL_TOTAL_FMT, (unsigned)yz_book_done_count(book),
                          (unsigned)YZ_ENTRY_COUNT);
    const int first = first_row(book->vol_sel);
    for (int r = 0; r < ROWS; r++) {
        const int c = first + r;
        if (c >= YZ_CHAPTER_COUNT) {
            lv_obj_add_flag(s_rows[r], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_rows[r], LV_OBJ_FLAG_HIDDEN);
        const bool sel = c == book->vol_sel;
        const unsigned done = yz_book_chapter_done(book, c);
        const unsigned total = YZ_CHAPTERS[c].count;
        lv_obj_set_style_bg_opa(s_rows[r], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (sel) lv_obj_remove_flag(s_marks[r], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_marks[r], LV_OBJ_FLAG_HIDDEN);
        // 碑文卷用墨色，分类卷用淡墨，选中行用朱色。
        const uint32_t ink = sel ? YZ_C_VERMILION : (YZ_CHAPTERS[c].text ? YZ_C_INK : YZ_C_INK_SOFT);
        lv_label_set_text_static(s_names[r], YZ_CHAPTERS[c].name);
        lv_obj_set_style_text_color(s_names[r], lv_color_hex(ink), 0);
        lv_obj_set_width(s_fills[r], done ? (int32_t)(1 + (BAR_W - 1) * done / total) : 0);
        lv_label_set_text_fmt(s_counts[r], YZ_STR_VOL_COUNT_FMT, done, total);
        lv_obj_set_style_text_color(s_counts[r], lv_color_hex(sel ? YZ_C_VERMILION : YZ_C_INK_SOFT), 0);
    }
    // 右侧滚动条：窗口在 14 卷中的位置。
    const int track = ROWS * ROW_H;
    const int h = track * ROWS / YZ_CHAPTER_COUNT;
    lv_obj_set_height(s_scroll, h);
    lv_obj_set_y(s_scroll, LIST_Y + (track - h) * first / (YZ_CHAPTER_COUNT - ROWS));
}

static bool build(lv_obj_t *scr, const yz_book_t *book) {
    yz_ui_box(scr, 22, 14, 4, 22, YZ_C_VERMILION, 1);
    lv_obj_t *head = yz_ui_label(scr, &yz_zh24, YZ_C_INK, 34, 10);
    lv_label_set_text_static(head, YZ_STR_VOL_TITLE);
    s_total = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 24, 44);

    for (int r = 0; r < ROWS; r++) {
        s_rows[r] = yz_ui_box(scr, 18, LIST_Y + r * ROW_H, 200, ROW_H - 2, YZ_C_PAPER_DEEP, 5);
        s_marks[r] = yz_ui_box(s_rows[r], 0, 5, 3, ROW_H - 12, YZ_C_VERMILION, 1);
        s_names[r] = yz_ui_label(s_rows[r], &yz_zh18, YZ_C_INK, 10, -1);
        s_tracks[r] = yz_ui_box(s_rows[r], BAR_X, (ROW_H - 2) / 2 - 2, BAR_W, 4, YZ_C_INK_SOFT, 2);
        lv_obj_set_style_bg_opa(s_tracks[r], LV_OPA_20, 0);
        s_fills[r] = yz_ui_box(s_rows[r], BAR_X, (ROW_H - 2) / 2 - 2, 0, 4, YZ_C_VERMILION, 2);
        s_counts[r] = yz_ui_label(s_rows[r], &yz_zh14, YZ_C_INK_SOFT, BAR_X + BAR_W + 4, 2);
        lv_obj_set_width(s_counts[r], 200 - (BAR_X + BAR_W + 4) - 6);
        lv_obj_set_style_text_align(s_counts[r], LV_TEXT_ALIGN_RIGHT, 0);
    }
    s_scroll = yz_ui_box(scr, 222, LIST_Y, 3, ROW_H, YZ_C_INK_SOFT, 1);
    lv_obj_set_style_bg_opa(s_scroll, LV_OPA_50, 0);

    lv_obj_t *hint = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 0, 0);
    lv_label_set_text_static(hint, YZ_STR_VOL_HINT);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);

    update(book);
    return false;
}

static void forget(void) {
    s_total = s_scroll = NULL;
    for (int r = 0; r < ROWS; r++) {
        s_rows[r] = s_marks[r] = s_names[r] = s_tracks[r] = s_fills[r] = s_counts[r] = NULL;
    }
}

const yz_page_t YZ_PAGE_VOLUMES = { build, update, forget };
