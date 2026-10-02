// tests/test_yz_book.c —— 字帖状态机：页面跳转、目录与临帖按键、跨卷翻页、收藏快照、计时临写、设置。
#include <string.h>

#include "yz_book.h"
#include "yz_test.h"

static yz_book_t b;

static void fresh(void) {
    yz_cfg_t cfg;
    yz_progress_t prog;
    yz_cfg_default(&cfg);
    yz_progress_reset(&prog);
    yz_book_init(&b, &cfg, &prog);
}

static uint32_t key(yz_btn_t btn, yz_ev_t ev) {
    return yz_book_input(&b, btn, ev);
}

static void open_home_item(int item) {
    while (b.home_sel != item) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
}

static void test_catalog_integrity(void) {
    // 章节首尾相接、覆盖全部字；每个字的章节查询一致。
    int next = 0;
    for (int c = 0; c < YZ_CHAPTER_COUNT; c++) {
        CHECK(YZ_CHAPTERS[c].first == next);
        CHECK(YZ_CHAPTERS[c].count > 0);
        for (int i = 0; i < YZ_CHAPTERS[c].count; i++) CHECK(yz_catalog_chapter_of(next + i) == c);
        next += YZ_CHAPTERS[c].count;
    }
    CHECK(next == YZ_ENTRY_COUNT);
    CHECK(yz_catalog_chapter_of(-1) == -1 && yz_catalog_chapter_of(YZ_ENTRY_COUNT) == -1);
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) {
        const yz_entry_t *e = &YZ_ENTRIES[i];
        CHECK(e->structure < YZ_STRUCT_COUNT && e->focus < YZ_FOCUS_COUNT);
        CHECK(e->page >= 1 && e->page <= 21 && e->side <= 1);
        CHECK(strstr(e->phrase, e->trad) != NULL);   // 碑文语境里一定有本字
        CHECK(e->simp[0] && e->pinyin[0]);
    }
}

static void test_home_and_back(void) {
    fresh();
    CHECK(b.screen == YZ_SCR_HOME && b.home_sel == 0);
    CHECK(key(YZ_BTN_UP, YZ_EV_CLICK) == YZ_FX_REFRESH);
    CHECK(b.home_sel == YZ_HOME_COUNT - 1);             // 上移回绕
    CHECK(key(YZ_BTN_UP, YZ_EV_PRESS) == 0);            // 按下事件不处理
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) == 0);             // 主页长按无动作

    open_home_item(YZ_HOME_ABOUT);
    CHECK(b.screen == YZ_SCR_ABOUT && b.about_page == 0);
    key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(b.about_page == YZ_ABOUT_PAGES - 1);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.about_page == 0);
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) & YZ_FX_SCREEN);
    CHECK(b.screen == YZ_SCR_HOME);

    // 继续临帖：进入当前字所在章节。
    b.prog.current = 80;
    open_home_item(YZ_HOME_CONTINUE);
    CHECK(b.screen == YZ_SCR_PRACTICE);
    CHECK(yz_book_entry(&b) == 80);
    CHECK(b.tab == yz_catalog_chapter_of(80));
}

static void test_catalog_navigation(void) {
    fresh();
    open_home_item(YZ_HOME_CATALOG);
    CHECK(b.screen == YZ_SCR_CATALOG && b.tab == 0 && b.pos == 0);

    // 上移越过全书第一字：回到最后一卷最后一字。
    uint32_t fx = key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_SCREEN);
    CHECK(yz_book_entry(&b) == YZ_ENTRY_COUNT - 1);
    CHECK(b.tab == YZ_CHAPTER_COUNT - 1);
    // 末行可见。
    CHECK(b.pos / YZ_CATALOG_COLS >= b.first_row && b.pos / YZ_CATALOG_COLS < b.first_row + YZ_CATALOG_ROWS);

    // 下移越过最后一字：回到第一字。
    fx = key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(yz_book_entry(&b) == 0 && (fx & YZ_FX_SCREEN));

    // 卷内移动只刷新，不重建页面。
    fx = key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(fx == (YZ_FX_REFRESH | YZ_FX_GLYPH));
    // 双击跳一行；跨过卷尾进入下一卷。
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(yz_book_entry(&b) == 6);
    for (int i = 0; i < 3; i++) key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(yz_book_entry(&b) == 21 && b.tab == 1);       // 篇首 19 字之后
    CHECK(b.pos == 21 - YZ_CHAPTERS[1].first);

    // 长按换卷，含收藏页签，首尾循环。
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.tab == 2 && b.pos == 0 && b.first_row == 0);
    for (int i = 0; i < 4; i++) key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.tab == YZ_FAV_TAB && b.list_len == 0);
    CHECK(yz_book_entry(&b) == -1);
    CHECK(key(YZ_BTN_OK, YZ_EV_CLICK) == 0);            // 空收藏不能进入临帖
    CHECK(key(YZ_BTN_DOWN, YZ_EV_CLICK) == (YZ_FX_REFRESH | YZ_FX_GLYPH));
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.tab == 0);
    key(YZ_BTN_UP, YZ_EV_LONG);
    CHECK(b.tab == YZ_FAV_TAB);

    // 滚动：在 52 字的“左右”卷里一直下移，选中行始终可见。
    key(YZ_BTN_UP, YZ_EV_LONG);
    key(YZ_BTN_UP, YZ_EV_LONG);
    key(YZ_BTN_UP, YZ_EV_LONG);
    CHECK(b.tab == 3 && b.list_len == 52);
    for (int i = 0; i < 51; i++) {
        key(YZ_BTN_DOWN, YZ_EV_CLICK);
        const int row = b.pos / YZ_CATALOG_COLS;
        CHECK(row >= b.first_row && row < b.first_row + YZ_CATALOG_ROWS);
    }
    CHECK(b.pos == 51 && b.first_row == 7);
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) & YZ_FX_SCREEN);
    CHECK(b.screen == YZ_SCR_HOME);
}

static void test_practice_keys(void) {
    fresh();
    open_home_item(YZ_HOME_CONTINUE);
    const int e0 = yz_book_entry(&b);
    CHECK(e0 == 0 && b.screen == YZ_SCR_PRACTICE);

    // 笔法卡开合。
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.card);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(!b.card);

    // 收藏：双击切换，并给出提示条与保存。
    uint32_t fx = key(YZ_BTN_OK, YZ_EV_DOUBLE);
    CHECK(yz_book_is_fav(&b, e0) && b.toast == YZ_TOAST_FAV_ON);
    CHECK(fx & YZ_FX_SAVE_PROG && fx & YZ_FX_TOAST);
    key(YZ_BTN_OK, YZ_EV_DOUBLE);
    CHECK(!yz_book_is_fav(&b, e0) && b.toast == YZ_TOAST_FAV_OFF);

    // 格线 / 底色循环。
    for (int i = 0; i < YZ_GRID_COUNT; i++) {
        CHECK(b.cfg.grid == (uint8_t)i);
        fx = key(YZ_BTN_UP, YZ_EV_LONG);
        CHECK(fx & YZ_FX_SAVE_CFG);
    }
    CHECK(b.cfg.grid == YZ_GRID_MI);
    key(YZ_BTN_UP, YZ_EV_DOUBLE);
    CHECK(b.cfg.ink == YZ_INK_PAPER && b.toast == YZ_TOAST_INK);
    key(YZ_BTN_UP, YZ_EV_DOUBLE);
    key(YZ_BTN_UP, YZ_EV_DOUBLE);
    CHECK(b.cfg.ink == YZ_INK_STONE);

    // 记一遍：计数饱和于 255。
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(b.prog.count[e0] == 1 && b.prog.sessions == 1 && b.toast == YZ_TOAST_MARKED);
    b.prog.count[e0] = 255;
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(b.prog.count[e0] == 255 && b.prog.sessions == 2);

    // 换字：记住当前字，并请求解码与保存。
    fx = key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_GLYPH && fx & YZ_FX_SAVE_PROG);
    CHECK(yz_book_entry(&b) == 1 && b.prog.current == 1);

    // 长按确定回目录，停在当前字。
    fx = key(YZ_BTN_OK, YZ_EV_LONG);
    CHECK(b.screen == YZ_SCR_CATALOG && yz_book_entry(&b) == 1 && (fx & YZ_FX_SCREEN));

    // 提示条到时消失。
    CHECK(b.toast != YZ_TOAST_NONE);
    CHECK(yz_book_tick(&b, YZ_TOAST_MS - 1) == 0);
    CHECK(yz_book_tick(&b, 1) == YZ_FX_TOAST && b.toast == YZ_TOAST_NONE);
}

static void test_timer(void) {
    fresh();
    b.cfg.timer_idx = 0;                                // 30 秒
    open_home_item(YZ_HOME_CONTINUE);
    key(YZ_BTN_OK, YZ_EV_CLICK);                        // 打开笔法卡
    uint32_t fx = key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.timing && b.timer_left_ms == 30000 && !b.card && b.toast == YZ_TOAST_TIMER_ON);
    CHECK(fx & YZ_FX_REFRESH);

    // 秒数不变时不刷新，变了才刷新。
    fx = yz_book_tick(&b, YZ_TOAST_MS);                 // 同时让提示条消失
    CHECK(fx & YZ_FX_REFRESH && b.timer_left_ms == 30000 - YZ_TOAST_MS);
    CHECK(yz_book_tick(&b, 100) == 0);

    // 计时中换字：已临时长计入，新字重新计时。
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(b.timing && b.timer_left_ms == 30000 && b.prog.seconds == 1);

    // 一遍完成：计数、提示音、自动翻到下一字并继续计时。
    const int e1 = yz_book_entry(&b);
    fx = yz_book_tick(&b, 30000);
    CHECK(b.prog.count[e1] == 1 && b.prog.sessions == 1 && b.prog.seconds == 31);
    CHECK(fx & YZ_FX_CHIME && fx & YZ_FX_SAVE_PROG && fx & YZ_FX_GLYPH);
    CHECK(b.toast == YZ_TOAST_TIMER_DONE);
    CHECK(yz_book_entry(&b) == e1 + 1 && b.timing && b.timer_left_ms == 30000);

    // 关闭提示音与自动翻页：完成后停在原字，计时停止。
    b.cfg.sound = 0;
    b.cfg.auto_next = 0;
    const int e2 = yz_book_entry(&b);
    fx = yz_book_tick(&b, 45000);
    CHECK(!(fx & YZ_FX_CHIME) && !b.timing && yz_book_entry(&b) == e2 && b.prog.count[e2] == 1);
    CHECK(b.prog.seconds == 61);

    // 手动停止：记入已临秒数。
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    yz_book_tick(&b, 10500);
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(!b.timing && b.prog.seconds == 71 && b.toast == YZ_TOAST_TIMER_OFF);

    // 离开临帖页时计时停止；不在临帖页时 tick 不计时。
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    key(YZ_BTN_OK, YZ_EV_LONG);
    CHECK(!b.timing && b.screen == YZ_SCR_CATALOG);
    b.timing = true;                                    // 构造异常状态：不在临帖页
    b.timer_left_ms = 10;
    yz_book_tick(&b, 1000);
    CHECK(b.timer_left_ms == 10);
}

static void test_favorites_snapshot(void) {
    fresh();
    b.prog.fav[0] = 0x05;                               // 第 0、2 字
    b.prog.fav[YZ_FAV_BYTES - 1] = 0x01;                // 倒数某字
    const int last = (YZ_FAV_BYTES - 1) * 8;
    CHECK(yz_book_fav_count(&b) == 3);
    open_home_item(YZ_HOME_FAVORITES);
    CHECK(b.screen == YZ_SCR_CATALOG && b.tab == YZ_FAV_TAB && b.list_len == 3);
    CHECK(b.list[0] == 0 && b.list[1] == 2 && b.list[2] == last);
    key(YZ_BTN_UP, YZ_EV_CLICK);                        // 收藏页签内循环
    CHECK(yz_book_entry(&b) == last);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.screen == YZ_SCR_PRACTICE);
    // 取消收藏后列表不变，仍可在快照里翻页。
    key(YZ_BTN_OK, YZ_EV_DOUBLE);
    CHECK(!yz_book_is_fav(&b, last) && b.list_len == 3);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(yz_book_entry(&b) == 0);
    // 回主页再进收藏：快照重建。
    key(YZ_BTN_OK, YZ_EV_LONG);
    key(YZ_BTN_OK, YZ_EV_LONG);
    open_home_item(YZ_HOME_FAVORITES);
    CHECK(b.list_len == 2);
}

static void test_settings(void) {
    fresh();
    open_home_item(YZ_HOME_SETTINGS);
    CHECK(b.screen == YZ_SCR_SETTINGS && b.set_sel == 0);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.cfg.grid == YZ_GRID_TIAN);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    for (int i = 0; i < YZ_TIMER_OPTIONS; i++) key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.cfg.timer_idx == 1);                        // 转一圈回到 60 秒
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.cfg.auto_next == 0);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.cfg.sound == 0);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(key(YZ_BTN_OK, YZ_EV_CLICK) & YZ_FX_BRIGHT);
    CHECK(b.cfg.bright_idx == 3);

    // 清除进度：第一次只是待确认；移开选择即取消；连按两次才清除。
    b.prog.count[5] = 3;
    b.prog.fav[0] = 1;
    b.prog.current = 40;
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(b.set_sel == YZ_SET_RESET);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.confirm_reset && b.prog.count[5] == 3);
    key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(!b.confirm_reset);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    const uint32_t fx = key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_SAVE_PROG && b.toast == YZ_TOAST_RESET);
    CHECK(b.prog.count[5] == 0 && b.prog.fav[0] == 0 && b.prog.current == 0);
    // 设置不受清除影响。
    CHECK(b.cfg.grid == YZ_GRID_TIAN && b.cfg.bright_idx == 3);
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) & YZ_FX_SCREEN);
    CHECK(b.screen == YZ_SCR_HOME);
}

static void test_init_sanitizes(void) {
    yz_cfg_t cfg;
    yz_progress_t prog;
    yz_cfg_default(&cfg);
    yz_progress_reset(&prog);
    prog.current = YZ_ENTRY_COUNT + 5;
    yz_book_init(&b, &cfg, &prog);
    CHECK(b.prog.current == 0 && yz_book_entry(&b) == 0);
    cfg.timer_idx = 99;
    CHECK(yz_book_timer_total_ms(&cfg) == 60000);
}

int main(void) {
    test_catalog_integrity();
    test_home_and_back();
    test_catalog_navigation();
    test_practice_keys();
    test_timer();
    test_favorites_snapshot();
    test_settings();
    test_init_sanitizes();
    printf("test_yz_book: PASS\n");
    return 0;
}
