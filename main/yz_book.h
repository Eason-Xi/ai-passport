// main/yz_book.h —— 字帖应用状态机（纯 C，无 ESP-IDF / LVGL 依赖，主机可测）。
//
// 应用任务把按键事件与时间流逝交给本模块，模块只改状态并返回“副作用”标志，
// 由应用任务去刷新界面、解码字形、保存、播放提示音。
//
// 多本字帖：同一时间只打开一本（prog.book）。目录页签、翻字、收藏列表、统计都限定在
// 当前字帖内；“字帖目录”页用来换帖，每本帖各自记住上次临到的字。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yz_catalog.h"

typedef enum { YZ_BTN_UP = 0, YZ_BTN_DOWN, YZ_BTN_OK } yz_btn_t;
typedef enum { YZ_EV_PRESS = 0, YZ_EV_CLICK, YZ_EV_DOUBLE, YZ_EV_LONG } yz_ev_t;

typedef enum {
    YZ_SCR_HOME = 0,
    YZ_SCR_CATALOG,
    YZ_SCR_PRACTICE,
    YZ_SCR_ABOUT,
    YZ_SCR_SETTINGS,
    YZ_SCR_LIBRARY,          // 字帖目录：选择字帖
} yz_screen_t;

typedef enum { YZ_GRID_MI = 0, YZ_GRID_TIAN, YZ_GRID_JIU, YZ_GRID_NONE, YZ_GRID_COUNT } yz_grid_t;
typedef enum { YZ_INK_STONE = 0, YZ_INK_PAPER, YZ_INK_TRACE, YZ_INK_COUNT } yz_ink_t;

typedef enum {
    YZ_HOME_CONTINUE = 0,
    YZ_HOME_CATALOG,
    YZ_HOME_FAVORITES,
    YZ_HOME_LIBRARY,
    YZ_HOME_ABOUT,
    YZ_HOME_SETTINGS,
    YZ_HOME_COUNT,
} yz_home_item_t;

typedef enum {
    YZ_SET_GRID = 0,
    YZ_SET_INK,
    YZ_SET_TIMER,
    YZ_SET_AUTO,
    YZ_SET_SOUND,
    YZ_SET_BRIGHT,
    YZ_SET_RESET,
    YZ_SET_COUNT,
} yz_setting_t;

typedef enum {
    YZ_TOAST_NONE = 0,
    YZ_TOAST_FAV_ON,
    YZ_TOAST_FAV_OFF,
    YZ_TOAST_GRID,
    YZ_TOAST_INK,
    YZ_TOAST_TIMER_ON,
    YZ_TOAST_TIMER_OFF,
    YZ_TOAST_TIMER_DONE,
    YZ_TOAST_MARKED,
    YZ_TOAST_RESET,
} yz_toast_t;

#define YZ_TIMER_OPTIONS 6
#define YZ_BRIGHT_OPTIONS 4
#define YZ_ABOUT_PAGES 5
#define YZ_FAV_BYTES ((YZ_ENTRY_COUNT + 7) / 8)
#define YZ_CATALOG_COLS 5
#define YZ_CATALOG_ROWS 4
#define YZ_TOAST_MS 1500

extern const uint16_t YZ_TIMER_SECONDS[YZ_TIMER_OPTIONS];
extern const uint8_t YZ_BRIGHT_PERCENT[YZ_BRIGHT_OPTIONS];

typedef enum {
    YZ_FX_SCREEN = 1u << 0,      // 切换 / 重建页面
    YZ_FX_REFRESH = 1u << 1,     // 刷新当前页面的动态内容
    YZ_FX_GLYPH = 1u << 2,       // 当前显示的拓本字变了，需要重新解码
    YZ_FX_SAVE_CFG = 1u << 3,
    YZ_FX_SAVE_PROG = 1u << 4,
    YZ_FX_CHIME = 1u << 5,       // 计时结束提示音
    YZ_FX_BRIGHT = 1u << 6,      // 亮度设置变了
    YZ_FX_TOAST = 1u << 7,       // 显示 / 隐藏提示条
} yz_fx_t;

typedef struct {
    uint8_t grid;        // yz_grid_t
    uint8_t ink;         // yz_ink_t
    uint8_t timer_idx;   // YZ_TIMER_SECONDS 下标
    uint8_t auto_next;   // 计时结束后自动翻到下一字并继续计时
    uint8_t sound;       // 计时结束提示音
    uint8_t bright_idx;  // YZ_BRIGHT_PERCENT 下标
} yz_cfg_t;

typedef struct {
    uint8_t book;                          // 当前打开的字帖
    uint16_t current[YZ_BOOK_COUNT];       // 每本帖最近临写的字（全局下标）
    uint8_t count[YZ_ENTRY_COUNT];         // 每字临写遍数，饱和于 255
    uint8_t fav[YZ_FAV_BYTES];             // 收藏位图（全局下标）
    uint32_t sessions[YZ_BOOK_COUNT];      // 每本帖累计临写遍数
    uint32_t seconds[YZ_BOOK_COUNT];       // 每本帖计时临写累计秒数
} yz_progress_t;

typedef struct {
    yz_screen_t screen;
    yz_cfg_t cfg;
    yz_progress_t prog;

    uint8_t home_sel;

    uint8_t lib_sel;             // 字帖目录里选中的字帖

    // 浏览列表：目录与临帖共用。tab 是当前字帖内的章节序号，等于章节数时是“收藏”页签。
    // 普通章节按顺序排列；收藏页签是进入时的快照，临帖中取消收藏不会让当前字从列表里消失。
    uint8_t tab;
    uint16_t list[YZ_BOOK_MAX_ENTRIES];
    uint16_t list_len;
    uint16_t pos;
    uint16_t first_row;          // 目录网格可见区的第一行

    bool card;                   // 临帖页的笔法卡
    bool timing;
    uint32_t timer_total_ms;
    uint32_t timer_left_ms;

    uint8_t about_page;
    uint8_t set_sel;
    bool confirm_reset;

    yz_toast_t toast;
    uint32_t toast_left_ms;
} yz_book_t;

void yz_cfg_default(yz_cfg_t *cfg);
void yz_progress_reset(yz_progress_t *prog);

// 用已读取（或默认）的设置与进度初始化，停在主页。
void yz_book_init(yz_book_t *book, const yz_cfg_t *cfg, const yz_progress_t *prog);

uint32_t yz_book_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev);
uint32_t yz_book_tick(yz_book_t *book, uint32_t elapsed_ms);

// 当前列表位置上的字（全局下标；列表为空时返回 -1）。
int yz_book_entry(const yz_book_t *book);
bool yz_book_is_fav(const yz_book_t *book, int entry);
// 某本字帖的收藏字数 / 临写过的字数。
uint16_t yz_book_fav_count(const yz_book_t *book, int b);
uint16_t yz_book_done_count(const yz_book_t *book, int b);
// 当前字帖的“收藏”页签序号与页签总数。
uint8_t yz_book_fav_tab(const yz_book_t *book);
uint8_t yz_book_tab_count(const yz_book_t *book);
uint32_t yz_book_timer_total_ms(const yz_cfg_t *cfg);
