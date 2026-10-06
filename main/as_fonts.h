// main/as_fonts.h —— 应用字体（Source Han Sans SC 子集，由 tools/gen_asmr_fonts.py 生成）。
//
// as_zh14 / as_zh18 / as_zh28：as_strings.h 中全部字符 + 可打印 ASCII。
// as_num48：定时分钟与呼吸倒数的大号数字（0–9、":"、"-"）。
// 所有标签必须显式指定这些字体；默认的 Montserrat 不含中文。
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(as_zh14)
LV_FONT_DECLARE(as_zh18)
LV_FONT_DECLARE(as_zh28)
LV_FONT_DECLARE(as_num48)

#define AS_FONT_SMALL (&as_zh14)
#define AS_FONT_BODY (&as_zh18)
#define AS_FONT_TITLE (&as_zh28)
#define AS_FONT_BIG (&as_num48)

// 启动自检：逐个码点检查各字体是否真的含有需要的字形（占位框视为缺失）。
// 必须在 LVGL 初始化之后、持有 LVGL 锁（或在 LVGL 任务内）调用。返回缺失的字形数。
int as_fonts_selfcheck(void);
