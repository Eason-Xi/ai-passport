// main/yz_catalog.h —— 《多宝塔碑》字帖字目：章节与单字的文字资料（纯数据，无 ESP-IDF / LVGL 依赖）。
//
// 数据由 tools/gen_yz_assets.py 依据 tools/yz_catalog.json 生成到 yz_catalog_data.c；
// 字形包 assets/images/yz_glyphs.bin 中第 i 个字形对应 YZ_ENTRIES[i]。
#pragma once

#include <stdint.h>

#define YZ_CHAPTER_COUNT 6
#define YZ_ENTRY_COUNT 188

// 结构类型：决定“结构要点”文字。
typedef enum {
    YZ_STRUCT_SINGLE = 0,   // 独体
    YZ_STRUCT_LR,           // 左右（含左中右）
    YZ_STRUCT_TB,           // 上下（含上中下）
    YZ_STRUCT_EN,           // 包围 / 半包围
    YZ_STRUCT_COUNT,
} yz_struct_t;

// 笔法重点：决定“笔法要点”文字。
typedef enum {
    YZ_FOCUS_HENG = 0,      // 横
    YZ_FOCUS_SHU,           // 竖
    YZ_FOCUS_PIE,           // 撇
    YZ_FOCUS_NA,            // 捺
    YZ_FOCUS_DIAN,          // 点
    YZ_FOCUS_GOU,           // 钩
    YZ_FOCUS_ZHE,           // 折
    YZ_FOCUS_ZOUZHI,        // 走之
    YZ_FOCUS_BAOGAI,        // 宝盖
    YZ_FOCUS_FANFU,         // 繁复笔画
    YZ_FOCUS_YONG,          // 永字八法
    YZ_FOCUS_COUNT,
} yz_focus_t;

typedef struct {
    const char *name;       // 章节名（两字）
    uint16_t first;         // 第一个字在 YZ_ENTRIES 中的下标
    uint16_t count;
} yz_chapter_t;

typedef struct {
    const char *simp;       // 简体
    const char *trad;       // 碑上原字（繁体 / 异体）
    const char *pinyin;     // 带声调拼音
    const char *phrase;     // 碑文语境（繁体原文，含本字）
    uint8_t structure;      // yz_struct_t
    uint8_t focus;          // yz_focus_t
    uint8_t page;           // 宋拓本第几开（1 起）
    uint8_t side;           // 0 = 右页，1 = 左页
} yz_entry_t;

extern const yz_chapter_t YZ_CHAPTERS[YZ_CHAPTER_COUNT];
extern const yz_entry_t YZ_ENTRIES[YZ_ENTRY_COUNT];

// 单字所属章节（越界返回 -1）。
int yz_catalog_chapter_of(int entry);
