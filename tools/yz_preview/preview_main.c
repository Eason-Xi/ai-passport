// tools/yz_preview/preview_main.c —— 在主机上用真实 LVGL 渲染字帖的各个页面。
// 由 tools/render_yz_preview.py 编译运行：链接固件里的界面与状态机代码，读入真实字形包，
// 按真实按键流程驱动 yz_book_t，逐页输出 PPM，并报告 LVGL 内存池占用与字形自检结果。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "yz_book.h"
#include "yz_fonts.h"
#include "yz_glyph.h"
#include "yz_strings.h"
#include "yz_theme.h"
#include "yz_ui.h"

static uint16_t s_frame[YZ_SCREEN_W * YZ_SCREEN_H];
static uint8_t s_render_buf[YZ_SCREEN_W * YZ_SCREEN_H * 2];
static uint32_t s_tick_ms;
static const char *s_out_dir = ".";
static size_t s_peak;
static yz_book_t b;

static uint32_t tick_cb(void) {
    return s_tick_ms;
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    const uint16_t *src = (const uint16_t *)px_map;
    const int w = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&s_frame[y * YZ_SCREEN_W + area->x1], src, (size_t)w * 2);
        src += w;
    }
    lv_display_flush_ready(display);
}

static void settle(void) {
    for (int i = 0; i < 8; i++) {
        s_tick_ms += 30;
        lv_timer_handler();
    }
}

static void capture(const char *name) {
    settle();
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_out_dir, name);
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fprintf(file, "P6\n%d %d\n255\n", YZ_SCREEN_W, YZ_SCREEN_H);
    for (int i = 0; i < YZ_SCREEN_W * YZ_SCREEN_H; i++) {
        const uint16_t c = s_frame[i];
        const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, bl = c & 0x1F;
        fputc((r << 3) | (r >> 2), file);
        fputc((g << 2) | (g >> 4), file);
        fputc((bl << 3) | (bl >> 2), file);
    }
    fclose(file);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const size_t used = mon.total_size - mon.free_size;
    if (mon.max_used > s_peak) s_peak = mon.max_used;
    printf("MEM %-26s used=%5zu peak=%5zu free=%5zu largest=%5zu\n", name, used, (size_t)mon.max_used,
           (size_t)mon.free_size, (size_t)mon.free_biggest_size);
}

// 与固件 yz_app.c 的 apply() 相同的界面副作用。
static void apply(uint32_t fx) {
    if (fx & YZ_FX_SCREEN) {
        yz_ui_show(&b);
        return;
    }
    if (fx & YZ_FX_GLYPH) yz_ui_glyph(&b);
    if (fx & YZ_FX_REFRESH) yz_ui_update(&b);
    if (fx & YZ_FX_TOAST) yz_ui_toast(&b);
}

static void key(yz_btn_t btn, yz_ev_t ev) {
    apply(yz_book_input(&b, btn, ev));
}

static void tick(uint32_t ms) {
    apply(yz_book_tick(&b, ms));
}

static void clear_toast(void) {
    tick(YZ_TOAST_MS + 10);
}

static void home(void) {
    if (b.screen != YZ_SCR_HOME) {
        if (b.screen == YZ_SCR_PRACTICE) key(YZ_BTN_OK, YZ_EV_LONG);
        key(YZ_BTN_OK, YZ_EV_LONG);
    }
    clear_toast();
}

static void home_open(int item) {
    home();
    while (b.home_sel != item) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
}

// 文字版式检查：按显式换行编写的文字不得被 LVGL 再折行；单行文字不得超出标签宽度。
static int text_fits(const char *name, const char *text, const lv_font_t *font, int width, int space) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, space, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int lines = 1;
    for (const char *c = text; *c; c++) lines += *c == '\n';
    const int line_h = lv_font_get_line_height(font);
    const int expected = lines * line_h + (lines - 1) * space;
    lv_point_t wrapped;
    lv_text_get_size(&wrapped, text, font, 0, space, width, LV_TEXT_FLAG_NONE);
    if (size.x > width || wrapped.y > expected) {
        printf("LAYOUT %s: width %d > %d or height %d > %d\n", name, (int)size.x, width, (int)wrapped.y, expected);
        return 1;
    }
    return 0;
}

static int check_layout(void) {
    static const char *const FOCUS[] = {
        YZ_STR_FOCUS_HENG, YZ_STR_FOCUS_SHU, YZ_STR_FOCUS_PIE, YZ_STR_FOCUS_NA, YZ_STR_FOCUS_DIAN,
        YZ_STR_FOCUS_GOU, YZ_STR_FOCUS_ZHE, YZ_STR_FOCUS_ZOUZHI, YZ_STR_FOCUS_BAOGAI, YZ_STR_FOCUS_FANFU,
        YZ_STR_FOCUS_YONG,
    };
    static const char *const TIPS[] = { YZ_STR_TIP_SINGLE, YZ_STR_TIP_LR, YZ_STR_TIP_TB, YZ_STR_TIP_EN };
    int bad = 0;
    char buf[160];
    for (size_t i = 0; i < sizeof FOCUS / sizeof FOCUS[0]; i++) {
        snprintf(buf, sizeof buf, "focus[%zu]", i);
        bad += text_fits(buf, FOCUS[i], &yz_zh14, YZ_CARD_TEXT_W, YZ_CARD_LINE_SPACE);
    }
    for (size_t i = 0; i < sizeof TIPS / sizeof TIPS[0]; i++) {
        snprintf(buf, sizeof buf, "struct[%zu]", i);
        bad += text_fits(buf, TIPS[i], &yz_zh14, YZ_CARD_TEXT_W, YZ_CARD_LINE_SPACE);
    }
    bad += text_fits("about2", YZ_STR_ABOUT_BODY_2, &yz_zh14, 194, 4);
    bad += text_fits("about3", YZ_STR_ABOUT_BODY_3, &yz_zh14, 118, 4);
    bad += text_fits("about3r", YZ_STR_ABOUT_BODY_3R, &yz_zh14, 76, 4);
    bad += text_fits("about4", YZ_STR_ABOUT_BODY_4, &yz_zh14, 118, 4);
    bad += text_fits("about4r", YZ_STR_ABOUT_BODY_4R, &yz_zh14, 76, 4);
    for (int k = 0; k < YZ_BOOK_COUNT; k++) {
        const yz_book_info_t *info = &YZ_BOOKS[k];
        bad += text_fits(info->name, info->intro, &yz_zh14, 194, 4);          // 简介第 1 页
        bad += text_fits(info->name, info->source_text, &yz_zh14, 194, 4);    // 简介第 5 页
        snprintf(buf, sizeof buf, YZ_STR_LIB_META_FMT, info->era, (unsigned)info->entry_count);
        bad += text_fits(info->name, buf, &yz_zh14, 196, 0);                  // 字帖目录
        // 简介两页正文最多 11 行，避免压到页码。
        int lines = 1;
        for (const char *c = info->intro; *c; c++) lines += *c == '\n';
        if (lines > 11) {
            printf("LAYOUT %s intro has %d lines\n", info->name, lines);
            bad++;
        }
    }
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) {
        const yz_entry_t *e = &YZ_ENTRIES[i];
        snprintf(buf, sizeof buf, YZ_STR_PHRASE_FMT, YZ_BOOKS[yz_catalog_book_of(i)].phrase_tag, e->phrase);
        bad += text_fits(e->phrase, buf, &yz_zh14, 196, 0);                 // 临帖页底部
        snprintf(buf, sizeof buf, YZ_STR_PREVIEW_PHRASE_FMT, e->phrase);
        bad += text_fits(e->phrase, buf, &yz_zh14, 120, 0);                 // 目录预览
        bad += text_fits(e->source, e->source, &yz_zh14, YZ_CARD_TEXT_W, 0); // 笔法卡出处
    }
    printf("LAYOUT problems=%d\n", bad);
    return bad;
}

static uint8_t *load_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return data;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: preview <out_dir> <yz_glyphs.bin>\n");
        return 1;
    }
    s_out_dir = argv[1];
    size_t pack_size = 0;
    uint8_t *pack_data = load_file(argv[2], &pack_size);
    static yz_glyph_pack_t pack;
    if (!pack_data || !yz_glyph_pack_open(&pack, pack_data, pack_size) || pack.count != YZ_ENTRY_COUNT) {
        fprintf(stderr, "glyph pack invalid: %s\n", argv[2]);
        return 1;
    }

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *display = lv_display_create(YZ_SCREEN_W, YZ_SCREEN_H);
    lv_display_set_buffers(display, s_render_buf, NULL, sizeof s_render_buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_cb);

    // 字形自检：固件启动时同一段代码；再加一个反例，保证检查本身不会无条件通过。
    int missing = yz_fonts_selfcheck();
    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(&yz_zh18, &dsc, 0x9F98, 0) && !dsc.is_placeholder) {
        printf("FONT-CHECK-BROKEN: U+9F98 reported as covered\n");
        missing++;
    }
    printf("FONT missing=%d\n", missing);
    missing += check_layout();

    yz_cfg_t cfg;
    yz_progress_t prog;
    yz_cfg_default(&cfg);
    yz_progress_reset(&prog);
    yz_book_init(&b, &cfg, &prog);
    yz_ui_init(&pack);
    yz_ui_set_battery(76);
    yz_ui_show(&b);
    capture("01_home_new");

    // 有进度的主页：若干字已临、两个收藏，停在“左右”卷。
    for (int i = 0; i < 40; i++) b.prog.count[(i * 7) % YZ_BOOKS[0].entry_count] = (uint8_t)(1 + i % 4);
    b.prog.sessions[0] = 86;
    b.prog.seconds[0] = 86 * 60;
    b.prog.current[0] = 74;   // 法
    b.prog.fav[74 / 8] |= (uint8_t)(1u << (74 % 8));
    b.prog.fav[57 / 8] |= (uint8_t)(1u << (57 % 8));
    yz_ui_set_battery(12);
    yz_ui_show(&b);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("02_home_progress_lowbat");
    yz_ui_set_battery(76);

    // 目录：从主页进入（定位到“法”），再逐字、跳行、换卷。
    home_open(YZ_HOME_CATALOG);
    capture("03_catalog_lr");
    for (int i = 0; i < 3; i++) key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("04_catalog_lr_scrolled");
    key(YZ_BTN_UP, YZ_EV_LONG);
    key(YZ_BTN_UP, YZ_EV_LONG);
    key(YZ_BTN_UP, YZ_EV_LONG);
    capture("05_catalog_title");
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    capture("06_catalog_numbers");

    // 临帖：拓本 · 米字格。
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("07_practice_stone_mi");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("08_practice_card");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    // 墨迹 · 田字格，收藏后的提示条。
    key(YZ_BTN_UP, YZ_EV_DOUBLE);
    key(YZ_BTN_UP, YZ_EV_LONG);
    clear_toast();
    key(YZ_BTN_OK, YZ_EV_DOUBLE);
    capture("09_practice_paper_tian_fav");
    clear_toast();
    // 描红 · 九宫格，计时进行中。
    key(YZ_BTN_UP, YZ_EV_DOUBLE);
    key(YZ_BTN_UP, YZ_EV_LONG);
    clear_toast();
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    clear_toast();
    tick(23 * 1000);
    capture("10_practice_trace_jiu_timer");
    // 计时结束：自动翻到下一字并继续计时，提示条报本帖总遍数。
    tick(40 * 1000);
    capture("11_practice_timer_done");
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    clear_toast();

    // 永字：笔法卡最长的一条；无格线。
    key(YZ_BTN_UP, YZ_EV_LONG);
    clear_toast();
    while (yz_book_entry(&b) != 57) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_UP, YZ_EV_DOUBLE);   // 回到拓本
    clear_toast();
    capture("12_practice_yong_nogrid");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("13_practice_yong_card");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    // 往前翻过本帖第一个字：首尾相接到本帖最后一个字。
    while (yz_book_entry(&b) != YZ_BOOKS[0].entry_count - 1) key(YZ_BTN_UP, YZ_EV_CLICK);
    capture("14_practice_last");

    // 收藏页签（有收藏）与空收藏。
    home_open(YZ_HOME_FAVORITES);
    capture("15_catalog_favorites");
    memset(b.prog.fav, 0, sizeof b.prog.fav);
    home_open(YZ_HOME_FAVORITES);
    capture("16_catalog_fav_empty");

    // 简介五页（多宝塔碑）。
    home_open(YZ_HOME_ABOUT);
    capture("17_about_1");
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("18_about_2");
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("19_about_3");
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("20_about_4");
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("20b_about_5");

    // 设置：选到“清除进度”并按一次确定（待确认）。
    home_open(YZ_HOME_SETTINGS);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("21_settings");
    key(YZ_BTN_UP, YZ_EV_CLICK);
    key(YZ_BTN_UP, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("22_settings_confirm_reset");

    // 逐字检查：多宝塔碑每个字的临帖页都渲染一次。
    home_open(YZ_HOME_CATALOG);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    for (int i = 0; i < YZ_BOOKS[0].entry_count; i++) {
        key(YZ_BTN_DOWN, YZ_EV_CLICK);
        settle();
    }
    capture("23_practice_after_full_loop");

    // 字帖目录：当前是多宝塔碑；选到颜勤礼碑；打开后回到它的主页。
    home_open(YZ_HOME_LIBRARY);
    capture("24_library_duobao");
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("25_library_qinli");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("26_home_qinli");

    // 颜勤礼碑：目录、临帖、笔法卡、描红、简介、来源。
    home_open(YZ_HOME_CATALOG);
    capture("27_catalog_qinli_title");
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    for (int i = 0; i < 7; i++) key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    capture("28_catalog_qinli_lr_scrolled");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    clear_toast();
    capture("29_practice_qinli");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("30_practice_qinli_card");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    key(YZ_BTN_UP, YZ_EV_DOUBLE);                 // 描红
    key(YZ_BTN_UP, YZ_EV_DOUBLE);
    key(YZ_BTN_UP, YZ_EV_LONG);                   // 米字格
    clear_toast();
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);               // 记一遍
    clear_toast();
    capture("31_practice_qinli_marked");
    home_open(YZ_HOME_ABOUT);
    capture("32_about_qinli_1");
    for (int i = 0; i < 4; i++) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("33_about_qinli_5");
    home();
    capture("34_home_qinli_progress");

    // 逐字检查：颜勤礼碑每个字。
    home_open(YZ_HOME_CATALOG);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    for (int i = 0; i < YZ_BOOKS[1].entry_count; i++) {
        key(YZ_BTN_DOWN, YZ_EV_CLICK);
        settle();
    }
    capture("35_practice_qinli_after_full_loop");

    // 回到字帖目录：颜勤礼碑标“当前”，选回多宝塔碑后长按返回（不换帖）。
    home_open(YZ_HOME_LIBRARY);
    key(YZ_BTN_UP, YZ_EV_CLICK);
    capture("36_library_back_to_duobao");
    key(YZ_BTN_OK, YZ_EV_LONG);
    capture("37_home_still_qinli");

    // 千字文：字帖目录选第三本 → 主页 → 全文目录（4 列，一行一句）→ 临帖 → 分类卷 → 简介。
    home_open(YZ_HOME_LIBRARY);
    while (b.lib_sel != 2) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("38_library_qianzi");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("39_home_qianzi");
    home_open(YZ_HOME_CATALOG);
    capture("40_catalog_qianzi_full");
    for (int i = 0; i < 9; i++) key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("41_catalog_qianzi_full_scrolled");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("42_practice_qianzi");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    capture("43_practice_qianzi_card");
    key(YZ_BTN_OK, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_LONG);
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    capture("44_catalog_qianzi_num");
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    capture("45_catalog_qianzi_single");
    home_open(YZ_HOME_ABOUT);
    capture("46_about_qianzi_1");
    for (int i = 0; i < 4; i++) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    capture("47_about_qianzi_5");

    // 逐字检查：千字文全文 1000 字，从第一字临到最后一字。
    home();
    b.prog.current[2] = (uint16_t)YZ_BOOKS[2].first_entry;
    home_open(YZ_HOME_CONTINUE);
    for (int i = 0; i < YZ_BOOKS[2].entry_count - 1; i++) {
        key(YZ_BTN_DOWN, YZ_EV_CLICK);
        settle();
    }
    capture("48_practice_qianzi_last");

    printf("PEAK used=%zu of %u\n", s_peak, (unsigned)LV_MEM_SIZE);
    free(pack_data);
    return missing ? 2 : 0;
}
