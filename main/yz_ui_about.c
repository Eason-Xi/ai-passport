// main/yz_ui_about.c —— 碑帖简介：五页（简介、颜体要诀、临帖按键、目录与主页、拓本来源）。
// 按键两页是左右两栏：左栏按键、右栏功能，两栏行高相同，逐行对齐。
#include "yz_ui_internal.h"

#define BODY_X 24
#define BODY_Y 50
#define BODY_W 194
#define LEFT_W 118
#define LINE_SPACE 4

static const char *const TITLES[YZ_ABOUT_PAGES] = {
    YZ_STR_ABOUT_TITLE_1, YZ_STR_ABOUT_TITLE_2, YZ_STR_ABOUT_TITLE_3, YZ_STR_ABOUT_TITLE_4, YZ_STR_ABOUT_TITLE_5,
};
static const char *const BODIES[YZ_ABOUT_PAGES] = {
    YZ_STR_ABOUT_BODY_1, YZ_STR_ABOUT_BODY_2, YZ_STR_ABOUT_BODY_3, YZ_STR_ABOUT_BODY_4, YZ_STR_ABOUT_BODY_5,
};
static const char *const RIGHT[YZ_ABOUT_PAGES] = {
    NULL, NULL, YZ_STR_ABOUT_BODY_3R, YZ_STR_ABOUT_BODY_4R, NULL,
};

static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_right;
static lv_obj_t *s_page;

static void update(const yz_book_t *book) {
    const uint8_t p = book->about_page < YZ_ABOUT_PAGES ? book->about_page : 0;
    lv_label_set_text_static(s_title, TITLES[p]);
    lv_label_set_text_static(s_body, BODIES[p]);
    if (RIGHT[p]) {
        lv_obj_set_width(s_body, LEFT_W);
        lv_obj_set_style_text_color(s_body, lv_color_hex(YZ_C_INK_SOFT), 0);
        lv_label_set_text_static(s_right, RIGHT[p]);
        lv_obj_remove_flag(s_right, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_width(s_body, BODY_W);
        lv_obj_set_style_text_color(s_body, lv_color_hex(YZ_C_INK), 0);
        lv_obj_add_flag(s_right, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(s_page, YZ_STR_PAGE_FMT, (unsigned)(p + 1), (unsigned)YZ_ABOUT_PAGES);
}

static lv_obj_t *body_label(lv_obj_t *scr, int x, int w) {
    lv_obj_t *l = yz_ui_label(scr, &yz_zh14, YZ_C_INK, x, BODY_Y);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_line_space(l, LINE_SPACE, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

static bool build(lv_obj_t *scr, const yz_book_t *book) {
    yz_ui_box(scr, 22, 14, 4, 22, YZ_C_VERMILION, 1);
    s_title = yz_ui_label(scr, &yz_zh24, YZ_C_INK, 34, 10);
    s_body = body_label(scr, BODY_X, BODY_W);
    s_right = body_label(scr, BODY_X + LEFT_W, BODY_W - LEFT_W);
    s_page = yz_ui_label(scr, &yz_zh14, YZ_C_INK_SOFT, 150, 294);
    lv_obj_set_width(s_page, 66);
    lv_obj_set_style_text_align(s_page, LV_TEXT_ALIGN_RIGHT, 0);
    update(book);
    return false;
}

static void forget(void) {
    s_title = s_body = s_right = s_page = NULL;
}

const yz_page_t YZ_PAGE_ABOUT = { build, update, forget };
