// main/yz_ui.c —— 页面切换、共享拓本字缓冲、顶层电量与提示条。
//
// 内存：拓本字只有一份 176×176 A8 缓冲（约 30 KB 静态区），当前页面要显示哪个字就解码哪个；
// 字形包本身在 Flash 中按需读取，不进 RAM。
#include "yz_ui.h"

#include <stdio.h>

#include "misc/cache/lv_cache.h"   // lv_image_cache_drop()：lvgl.h 未导出
#include "yz_ui_internal.h"

static const yz_glyph_pack_t *s_pack;
static uint8_t s_glyph_buf[YZ_GLYPH_PIXELS];
static lv_image_dsc_t s_glyph_dsc;
static int s_decoded = -2;          // 缓冲里现在是哪个字（-1 = 空白）

static lv_obj_t *s_screen;
static const yz_page_t *s_page;
static lv_obj_t *s_glyph_img;       // 当前页面上显示拓本字的图像（可为 NULL）

static lv_obj_t *s_battery;         // 顶层：电量图标 + 百分比
static lv_obj_t *s_battery_body;
static lv_obj_t *s_battery_fill;
static lv_obj_t *s_battery_cap;
static lv_obj_t *s_battery_text;
static int s_soc = -1;
static bool s_dark;

static lv_obj_t *s_toast;
static lv_obj_t *s_toast_text;

// ---------------------------------------------------------------- 小工具

lv_obj_t *yz_ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *yz_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text_static(l, "");
    return l;
}

lv_obj_t *yz_ui_glyph_image(lv_obj_t *parent, uint16_t scale, uint32_t color) {
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_remove_style_all(img);
    lv_image_set_src(img, &s_glyph_dsc);
    lv_obj_set_style_image_recolor(img, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    if (scale != LV_SCALE_NONE) {
        lv_image_set_scale(img, scale);
        lv_image_set_antialias(img, true);
        // 缩放后对象本身仍按原尺寸布局：把对象尺寸设为缩放后大小，缩放中心放在左上角。
        const int32_t size = (int32_t)YZ_GLYPH_SIZE * scale / 256;
        lv_image_set_pivot(img, 0, 0);
        lv_obj_set_size(img, size, size);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_TOP_LEFT);
    }
    s_glyph_img = img;
    return img;
}

const char *yz_ui_grid_name(uint8_t grid) {
    static const char *const NAMES[YZ_GRID_COUNT] = {
        YZ_STR_GRID_MI, YZ_STR_GRID_TIAN, YZ_STR_GRID_JIU, YZ_STR_GRID_NONE,
    };
    return NAMES[grid < YZ_GRID_COUNT ? grid : 0];
}

const char *yz_ui_ink_name(uint8_t ink) {
    static const char *const NAMES[YZ_INK_COUNT] = { YZ_STR_INK_STONE, YZ_STR_INK_PAPER, YZ_STR_INK_TRACE };
    return NAMES[ink < YZ_INK_COUNT ? ink : 0];
}

const char *yz_ui_struct_name(uint8_t structure) {
    static const char *const NAMES[YZ_STRUCT_COUNT] = {
        YZ_STR_STRUCT_SINGLE, YZ_STR_STRUCT_LR, YZ_STR_STRUCT_TB, YZ_STR_STRUCT_EN,
    };
    return NAMES[structure < YZ_STRUCT_COUNT ? structure : 0];
}

const char *yz_ui_struct_tip(uint8_t structure) {
    static const char *const TIPS[YZ_STRUCT_COUNT] = {
        YZ_STR_TIP_SINGLE, YZ_STR_TIP_LR, YZ_STR_TIP_TB, YZ_STR_TIP_EN,
    };
    return TIPS[structure < YZ_STRUCT_COUNT ? structure : 0];
}

const char *yz_ui_focus_tip(uint8_t focus) {
    static const char *const TIPS[YZ_FOCUS_COUNT] = {
        YZ_STR_FOCUS_HENG, YZ_STR_FOCUS_SHU, YZ_STR_FOCUS_PIE, YZ_STR_FOCUS_NA,
        YZ_STR_FOCUS_DIAN, YZ_STR_FOCUS_GOU, YZ_STR_FOCUS_ZHE, YZ_STR_FOCUS_ZOUZHI,
        YZ_STR_FOCUS_BAOGAI, YZ_STR_FOCUS_FANFU, YZ_STR_FOCUS_YONG,
    };
    return TIPS[focus < YZ_FOCUS_COUNT ? focus : 0];
}

const char *yz_ui_tab_name(const yz_book_t *book, uint8_t tab) {
    const yz_book_info_t *info = &YZ_BOOKS[book->prog.book];
    return tab < info->chapter_count ? YZ_CHAPTERS[info->first_chapter + tab].name : YZ_STR_FAV_TAB;
}

// ---------------------------------------------------------------- 电量

static void battery_apply(void) {
    if (!s_battery) return;
    if (s_soc < 0) {
        lv_obj_add_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
    const uint32_t fg = s_dark ? YZ_C_STONE_SOFT : YZ_C_INK_SOFT;
    const uint32_t fill = s_soc <= 15 ? YZ_C_VERMILION : fg;
    lv_obj_set_style_border_color(s_battery_body, lv_color_hex(fg), 0);
    lv_obj_set_style_bg_color(s_battery_cap, lv_color_hex(fg), 0);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(fill), 0);
    lv_obj_set_width(s_battery_fill, 1 + 15 * s_soc / 100);
    lv_obj_set_style_text_color(s_battery_text, lv_color_hex(fg), 0);
    lv_label_set_text_fmt(s_battery_text, "%d", s_soc);
}

void yz_ui_battery_theme(bool dark) {
    s_dark = dark;
    battery_apply();
}

void yz_ui_set_battery(int soc) {
    s_soc = soc > 100 ? 100 : soc;
    battery_apply();
}

static void battery_create(void) {
    // 右上角：数字在左，电池图标在右；避开 30 px 圆角。
    s_battery = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_battery);
    lv_obj_set_pos(s_battery, 168, 11);
    lv_obj_set_size(s_battery, 52, 16);
    lv_obj_remove_flag(s_battery, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_battery_text = yz_ui_label(s_battery, &yz_zh14, YZ_C_INK_SOFT, 0, -1);
    lv_obj_set_width(s_battery_text, 26);
    lv_obj_set_style_text_align(s_battery_text, LV_TEXT_ALIGN_RIGHT, 0);

    s_battery_body = yz_ui_box(s_battery, 29, 3, 20, 11, YZ_C_PAPER, 2);
    lv_obj_set_style_bg_opa(s_battery_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_battery_body, 1, 0);
    s_battery_fill = yz_ui_box(s_battery_body, 2, 2, 8, 7, YZ_C_INK_SOFT, 1);
    s_battery_cap = yz_ui_box(s_battery, 49, 6, 2, 5, YZ_C_INK_SOFT, 0);
    battery_apply();
}

// ---------------------------------------------------------------- 提示条

void yz_ui_toast(const yz_book_t *book) {
    if (!s_toast) return;
    if (book->toast == YZ_TOAST_NONE) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const int entry = yz_book_entry(book);
    const unsigned count = entry >= 0 ? book->prog.count[entry] : 0;
    switch (book->toast) {
    case YZ_TOAST_FAV_ON: lv_label_set_text_static(s_toast_text, YZ_STR_TOAST_FAV_ON); break;
    case YZ_TOAST_FAV_OFF: lv_label_set_text_static(s_toast_text, YZ_STR_TOAST_FAV_OFF); break;
    case YZ_TOAST_GRID:
        lv_label_set_text_fmt(s_toast_text, YZ_STR_TOAST_GRID_FMT, yz_ui_grid_name(book->cfg.grid));
        break;
    case YZ_TOAST_INK:
        lv_label_set_text_fmt(s_toast_text, YZ_STR_TOAST_INK_FMT, yz_ui_ink_name(book->cfg.ink));
        break;
    case YZ_TOAST_TIMER_ON:
        lv_label_set_text_fmt(s_toast_text, YZ_STR_TOAST_TIMER_FMT,
                              (unsigned)(book->timer_total_ms / 1000u));
        break;
    case YZ_TOAST_TIMER_OFF: lv_label_set_text_static(s_toast_text, YZ_STR_TOAST_TIMER_OFF); break;
    case YZ_TOAST_TIMER_DONE: {
        // 自动翻页时当前字已经换成下一个：提示里报的是刚完成的总遍数。
        lv_label_set_text_fmt(s_toast_text, YZ_STR_TOAST_DONE_FMT,
                              (unsigned)book->prog.sessions[book->prog.book]);
        break;
    }
    case YZ_TOAST_MARKED: lv_label_set_text_fmt(s_toast_text, YZ_STR_TOAST_MARK_FMT, count); break;
    case YZ_TOAST_RESET: lv_label_set_text_static(s_toast_text, YZ_STR_TOAST_RESET); break;
    default: lv_label_set_text_static(s_toast_text, ""); break;
    }
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_toast);
    // 放在顶部：临帖页下方是简体、拼音与计时，不能遮住。
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, 40);
}

static void toast_create(void) {
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(YZ_C_INK), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_toast, 14, 0);
    lv_obj_set_style_pad_hor(s_toast, 14, 0);
    lv_obj_set_style_pad_ver(s_toast, 5, 0);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    s_toast_text = yz_ui_label(s_toast, &yz_zh14, YZ_C_PAPER, 0, 0);
    lv_obj_set_pos(s_toast_text, 0, 0);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------- 拓本字

static int glyph_entry_for(const yz_book_t *book) {
    switch (book->screen) {
    case YZ_SCR_HOME: return book->prog.current[book->prog.book];
    case YZ_SCR_LIBRARY: return YZ_BOOKS[book->lib_sel].emblem;
    case YZ_SCR_CATALOG:
    case YZ_SCR_PRACTICE: return yz_book_entry(book);
    default: return -1;
    }
}

static void decode(int entry) {
    if (entry == s_decoded) return;
    if (entry < 0 || !s_pack || !yz_glyph_decode(s_pack, entry, s_glyph_buf)) {
        lv_memzero(s_glyph_buf, sizeof(s_glyph_buf));
    }
    s_decoded = entry;
    lv_image_cache_drop(&s_glyph_dsc);
}

void yz_ui_glyph(const yz_book_t *book) {
    decode(glyph_entry_for(book));
    if (s_glyph_img) lv_obj_invalidate(s_glyph_img);
}

// ---------------------------------------------------------------- 页面

static const yz_page_t *page_for(yz_screen_t screen) {
    switch (screen) {
    case YZ_SCR_CATALOG: return &YZ_PAGE_CATALOG;
    case YZ_SCR_PRACTICE: return &YZ_PAGE_PRACTICE;
    case YZ_SCR_ABOUT: return &YZ_PAGE_ABOUT;
    case YZ_SCR_SETTINGS: return &YZ_PAGE_SETTINGS;
    case YZ_SCR_LIBRARY: return &YZ_PAGE_LIBRARY;
    case YZ_SCR_HOME:
    default: return &YZ_PAGE_HOME;
    }
}

void yz_ui_init(const yz_glyph_pack_t *pack) {
    s_pack = pack;
    s_glyph_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_glyph_dsc.header.cf = LV_COLOR_FORMAT_A8;
    s_glyph_dsc.header.w = YZ_GLYPH_SIZE;
    s_glyph_dsc.header.h = YZ_GLYPH_SIZE;
    s_glyph_dsc.header.stride = YZ_GLYPH_SIZE;
    s_glyph_dsc.data = s_glyph_buf;
    s_glyph_dsc.data_size = sizeof(s_glyph_buf);
    battery_create();
    toast_create();
}

void yz_ui_show(const yz_book_t *book) {
    decode(glyph_entry_for(book));
    lv_obj_t *old = s_screen;
    if (s_page) s_page->forget();
    s_glyph_img = NULL;

    // 先换上空白新屏并删除旧屏，再往新屏里建控件：内存池里同时只有一个页面的对象。
    // 整个过程都在 LVGL 锁内，渲染任务看不到半成品。
    s_page = page_for(book->screen);
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(YZ_C_PAPER), 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(s_screen);
    if (old) lv_obj_delete(old);
    yz_ui_battery_theme(s_page->build(s_screen, book));
    yz_ui_toast(book);
}

void yz_ui_update(const yz_book_t *book) {
    if (s_page) s_page->update(book);
}
