// main/yz_catalog.h —— 字帖字目：字帖、章节与单字的文字资料（纯数据，无 ESP-IDF / LVGL 依赖）。
//
// 数据由 tools/gen_yz_assets.py 依据 tools/yz_catalog.json 生成到 yz_catalog_data.c 与
// yz_catalog_size.h。所有字帖的字依次排成一个全局序列：字形包 assets/images/yz_glyphs.bin
// 中第 i 个字形对应 YZ_ENTRIES[i]；每本字帖占其中连续一段，章节也按字帖依次排列。
#pragma once

#include <stdint.h>

#include "yz_catalog_size.h"

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
    const char *name;           // 帖名（四字）
    const char *name_v;         // 竖排帖名（每字一行）
    const char *era;            // 立碑年代
    const char *intro;          // 简介正文（显式换行）
    const char *source_text;    // 拓本来源正文（显式换行）
    uint16_t emblem;            // 题签字：字帖目录里代表这本帖的字（全局下标）
    uint16_t first_chapter;     // 第一个章节在 YZ_CHAPTERS 中的下标
    uint16_t chapter_count;
    uint16_t first_entry;       // 第一个字在 YZ_ENTRIES 中的下标
    uint16_t entry_count;
} yz_book_info_t;

typedef struct {
    const char *name;       // 章节名（两字）
    uint16_t first;         // 第一个字在 YZ_ENTRIES 中的下标（全局）
    uint16_t count;
} yz_chapter_t;

typedef struct {
    const char *simp;       // 简体
    const char *trad;       // 碑上原字（繁体 / 异体）
    const char *pinyin;     // 带声调拼音
    const char *phrase;     // 碑文语境（繁体原文，含本字）
    const char *source;     // 拓本出处（册、页）
    uint8_t structure;      // yz_struct_t
    uint8_t focus;          // yz_focus_t
} yz_entry_t;

extern const yz_book_info_t YZ_BOOKS[YZ_BOOK_COUNT];
extern const yz_chapter_t YZ_CHAPTERS[YZ_CHAPTER_COUNT];
extern const yz_entry_t YZ_ENTRIES[YZ_ENTRY_COUNT];

// 单字所属的全局章节下标 / 字帖下标（越界返回 -1）。
int yz_catalog_chapter_of(int entry);
int yz_catalog_book_of(int entry);
