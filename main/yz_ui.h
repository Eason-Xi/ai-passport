// main/yz_ui.h —— 字帖界面。所有函数都必须在 LVGL 任务内或持有 bsp_lvgl_lock() 时调用。
#pragma once

#include "yz_book.h"
#include "yz_glyph.h"

// 建立顶层的电量与提示条；pack 是已打开的字形包（可为 NULL，此时拓本字显示为空白）。
void yz_ui_init(const yz_glyph_pack_t *pack);
// 按 book->screen 重建整个页面（含解码当前拓本字）。
void yz_ui_show(const yz_book_t *book);
// 只刷新当前页面的动态内容。
void yz_ui_update(const yz_book_t *book);
// 当前页面要显示的拓本字变了：重新解码并重绘。
void yz_ui_glyph(const yz_book_t *book);
// 显示 / 隐藏提示条。
void yz_ui_toast(const yz_book_t *book);
// 电量百分比；-1 表示不可用（隐藏）。
void yz_ui_set_battery(int soc);
