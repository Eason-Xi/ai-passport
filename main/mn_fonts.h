// main/mn_fonts.h —— 应用字体（Source Han Sans SC 子集，由 tools/gen_metronome_fonts.py 生成）。
//
// mn_zh14 / mn_zh18：mn_strings.h 中全部字符 + 可打印 ASCII。
// mn_num88：BPM 大号数字（0–9 与 "-"）。
// 所有控件必须显式指定这些字体；默认的 Montserrat 不含中文。
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(mn_zh14)
LV_FONT_DECLARE(mn_zh18)
LV_FONT_DECLARE(mn_num88)

#define MN_FONT_SMALL (&mn_zh14)
#define MN_FONT_BODY (&mn_zh18)
#define MN_FONT_BIG (&mn_num88)

// 启动自检：逐个码点检查各字体是否真的含有需要的字形（placeholder 视为缺失）。
// 必须在 LVGL 初始化之后、持有 LVGL 锁（或在 LVGL 任务内）调用。
// 返回缺失的字形数，并把缺失项打到日志里。
int mn_fonts_selfcheck(void);
