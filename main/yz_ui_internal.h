// main/yz_ui_internal.h —— 各页面共用的小工具（仅 yz_ui*.c 使用）。
#pragma once

#include <stdbool.h>

#include "lvgl.h"
#include "yz_book.h"
#include "yz_fonts.h"
#include "yz_strings.h"
#include "yz_theme.h"

typedef struct {
    // 在空白屏幕 scr 上建立页面；返回 true 表示背景是深色（电量图标改用浅色）。
    bool (*build)(lv_obj_t *scr, const yz_book_t *book);
    void (*update)(const yz_book_t *book);
    // 页面即将删除：清空本页保存的对象指针。
    void (*forget)(void);
} yz_page_t;

extern const yz_page_t YZ_PAGE_HOME;
extern const yz_page_t YZ_PAGE_CATALOG;
extern const yz_page_t YZ_PAGE_PRACTICE;
extern const yz_page_t YZ_PAGE_ABOUT;
extern const yz_page_t YZ_PAGE_SETTINGS;

// 去掉默认样式的基础对象 / 文字。
lv_obj_t *yz_ui_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg, int radius);
lv_obj_t *yz_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y);

// 显示共享拓本字缓冲的图像（A8，按 recolor 着色）；scale 为 LVGL 缩放（256 = 原大）。
// 每个页面最多一个。
lv_obj_t *yz_ui_glyph_image(lv_obj_t *parent, uint16_t scale, uint32_t color);

// 电量图标的深浅配色（临帖页切换底色时调用）。
void yz_ui_battery_theme(bool dark);

const char *yz_ui_grid_name(uint8_t grid);
const char *yz_ui_ink_name(uint8_t ink);
const char *yz_ui_struct_name(uint8_t structure);
const char *yz_ui_struct_tip(uint8_t structure);
const char *yz_ui_focus_tip(uint8_t focus);
const char *yz_ui_tab_name(uint8_t tab);
