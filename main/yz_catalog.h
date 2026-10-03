// main/yz_catalog.h —— 《多宝塔碑》字目：帖文资料、分卷与单字（纯数据，无 ESP-IDF / LVGL 依赖）。
//
// 数据由 tools/gen_yz_assets.py 依据 tools/yz_catalog.json 生成到 yz_catalog_data.c 与
// yz_catalog_size.h。YZ_ENTRIES 按碑文顺序排列全碑每一个字（重复的字各是一处原拓），
// 字形包 assets/images/yz_glyphs.bin 中第 i 个字形对应 YZ_ENTRIES[i]。
// 分卷是 YZ_CHAPTER_ITEMS 中的一段下标：前 YZ_TEXT_CHAPTER_COUNT 卷按碑文分段、首尾相接；
// 其后的分类卷（数目、独体、左右、上下、包围）每个不同的字只收一处，字形不重复存储。
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
    const char *author;         // 主页顶部的书者行
    const char *phrase_tag;     // 语境前缀“碑文”
    const char *intro;          // 简介正文（显式换行）
    const char *source_text;    // 拓本来源正文（显式换行）
} yz_book_info_t;

typedef struct {
    const char *name;       // 卷名（两字）
    uint16_t first;         // 第一个字在 YZ_CHAPTER_ITEMS 中的位置
    uint16_t count;
    uint8_t cols;           // 目录网格列数
    uint8_t text;           // 1 = 碑文卷（按碑文顺序的一段），0 = 分类卷
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

extern const yz_book_info_t YZ_BOOK;
extern const yz_chapter_t YZ_CHAPTERS[YZ_CHAPTER_COUNT];
extern const uint16_t YZ_CHAPTER_ITEMS[YZ_CHAPTER_ITEM_COUNT];   // 字下标
extern const yz_entry_t YZ_ENTRIES[YZ_ENTRY_COUNT];

// 第 chapter 卷第 pos 个字的下标。
static inline int yz_chapter_entry(int chapter, int pos) {
    return YZ_CHAPTER_ITEMS[YZ_CHAPTERS[chapter].first + pos];
}

// 单字所在的碑文卷，并给出在卷内的位置；越界返回 -1。
int yz_catalog_chapter_of(int entry, int *pos);
