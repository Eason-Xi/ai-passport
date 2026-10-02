// main/yz_fonts.h —— 中文字体子集（assets/fonts/yz_zh*.c，由 tools/gen_yz_fonts.py 生成）。
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(yz_zh14);
LV_FONT_DECLARE(yz_zh18);
LV_FONT_DECLARE(yz_zh24);
LV_FONT_DECLARE(yz_zh32);   // 只含字目单字、应用标题与数字

// 逐码点核对字体覆盖（lv_font_get_glyph_dsc 成功且不是占位符），返回缺失数量。
int yz_fonts_selfcheck(void);
