// tests/test_yz_book.c —— 字帖状态机：页面跳转、目录与临帖按键、本帖内翻页、收藏快照、计时临写、
// 设置、字帖目录换帖与各帖独立进度。
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

static void open_book(int which) {
    CHECK(b.screen == YZ_SCR_HOME);
    open_home_item(YZ_HOME_LIBRARY);
    CHECK(b.screen == YZ_SCR_LIBRARY && b.lib_sel == b.prog.book);
    while (b.lib_sel != which) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.screen == YZ_SCR_HOME && b.prog.book == which);
}

static void test_catalog_integrity(void) {
    // 字帖、章节、单字三张表首尾相接，覆盖全部字；查询函数与表一致。
    CHECK(YZ_BOOK_COUNT >= 2);
    int next_entry = 0, next_chapter = 0;
    for (int k = 0; k < YZ_BOOK_COUNT; k++) {
        const yz_book_info_t *info = &YZ_BOOKS[k];
        CHECK(info->first_entry == next_entry && info->first_chapter == next_chapter);
        CHECK(info->entry_count <= YZ_BOOK_MAX_ENTRIES && info->chapter_count <= YZ_BOOK_MAX_CHAPTERS);
        CHECK(yz_catalog_book_of(info->emblem) == k);
        int in_book = 0;
        for (int c = info->first_chapter; c < info->first_chapter + info->chapter_count; c++) {
            CHECK(YZ_CHAPTERS[c].first == next_entry + in_book && YZ_CHAPTERS[c].count > 0);
            for (int i = 0; i < YZ_CHAPTERS[c].count; i++) {
                CHECK(yz_catalog_chapter_of(YZ_CHAPTERS[c].first + i) == c);
                CHECK(yz_catalog_book_of(YZ_CHAPTERS[c].first + i) == k);
            }
            in_book += YZ_CHAPTERS[c].count;
        }
        CHECK(in_book == info->entry_count);
        next_entry += info->entry_count;
        next_chapter += info->chapter_count;
    }
    CHECK(next_entry == YZ_ENTRY_COUNT && next_chapter == YZ_CHAPTER_COUNT);
    CHECK(yz_catalog_chapter_of(-1) == -1 && yz_catalog_chapter_of(YZ_ENTRY_COUNT) == -1);
    CHECK(yz_catalog_book_of(-1) == -1 && yz_catalog_book_of(YZ_ENTRY_COUNT) == -1);
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) {
        const yz_entry_t *e = &YZ_ENTRIES[i];
        CHECK(e->structure < YZ_STRUCT_COUNT && e->focus < YZ_FOCUS_COUNT);
        CHECK(strstr(e->phrase, e->trad) != NULL);   // 碑文语境里一定有本字
        CHECK(e->simp[0] && e->pinyin[0] && e->source[0]);
    }
}

static void test_home_and_back(void) {
    fresh();
    CHECK(b.screen == YZ_SCR_HOME && b.home_sel == 0 && b.prog.book == 0);
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
    b.prog.current[0] = 80;
    open_home_item(YZ_HOME_CONTINUE);
    CHECK(b.screen == YZ_SCR_PRACTICE);
    CHECK(yz_book_entry(&b) == 80);
    CHECK(b.tab == yz_catalog_chapter_of(80));
}

static void test_catalog_navigation(void) {
    fresh();
    const yz_book_info_t *d = &YZ_BOOKS[0];
    open_home_item(YZ_HOME_CATALOG);
    CHECK(b.screen == YZ_SCR_CATALOG && b.tab == 0 && b.pos == 0);

    // 上移越过本帖第一字：回到本帖最后一卷最后一字（不会跑到别的字帖）。
    uint32_t fx = key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_SCREEN);
    CHECK(yz_book_entry(&b) == d->first_entry + d->entry_count - 1);
    CHECK(b.tab == d->chapter_count - 1);
    CHECK(b.pos / YZ_CATALOG_COLS >= b.first_row && b.pos / YZ_CATALOG_COLS < b.first_row + YZ_CATALOG_ROWS);

    // 下移越过本帖最后一字：回到本帖第一字。
    fx = key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(yz_book_entry(&b) == d->first_entry && (fx & YZ_FX_SCREEN));

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
    const uint8_t fav_tab = yz_book_fav_tab(&b);
    CHECK(fav_tab == d->chapter_count && yz_book_tab_count(&b) == fav_tab + 1);
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.tab == 2 && b.pos == 0 && b.first_row == 0);
    while (b.tab != fav_tab) key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.list_len == 0 && yz_book_entry(&b) == -1);
    CHECK(key(YZ_BTN_OK, YZ_EV_CLICK) == 0);            // 空收藏不能进入临帖
    CHECK(key(YZ_BTN_DOWN, YZ_EV_CLICK) == (YZ_FX_REFRESH | YZ_FX_GLYPH));
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.tab == 0);
    key(YZ_BTN_UP, YZ_EV_LONG);
    CHECK(b.tab == fav_tab);

    // 滚动：在 52 字的“左右”卷里一直下移，选中行始终可见。
    while (b.tab != 3) key(YZ_BTN_UP, YZ_EV_LONG);
    CHECK(b.list_len == 52);
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

    // 记一遍：计数饱和于 255，遍数记在当前字帖。
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(b.prog.count[e0] == 1 && b.prog.sessions[0] == 1 && b.toast == YZ_TOAST_MARKED);
    b.prog.count[e0] = 255;
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(b.prog.count[e0] == 255 && b.prog.sessions[0] == 2 && b.prog.sessions[1] == 0);

    // 换字：记住当前字，并请求解码与保存。
    fx = key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_GLYPH && fx & YZ_FX_SAVE_PROG);
    CHECK(yz_book_entry(&b) == 1 && b.prog.current[0] == 1);

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
    CHECK(b.timing && b.timer_left_ms == 30000 && b.prog.seconds[0] == 1);

    // 一遍完成：计数、提示音、自动翻到下一字并继续计时。
    const int e1 = yz_book_entry(&b);
    fx = yz_book_tick(&b, 30000);
    CHECK(b.prog.count[e1] == 1 && b.prog.sessions[0] == 1 && b.prog.seconds[0] == 31);
    CHECK(fx & YZ_FX_CHIME && fx & YZ_FX_SAVE_PROG && fx & YZ_FX_GLYPH);
    CHECK(b.toast == YZ_TOAST_TIMER_DONE);
    CHECK(yz_book_entry(&b) == e1 + 1 && b.timing && b.timer_left_ms == 30000);

    // 关闭提示音与自动翻页：完成后停在原字，计时停止。
    b.cfg.sound = 0;
    b.cfg.auto_next = 0;
    const int e2 = yz_book_entry(&b);
    fx = yz_book_tick(&b, 45000);
    CHECK(!(fx & YZ_FX_CHIME) && !b.timing && yz_book_entry(&b) == e2 && b.prog.count[e2] == 1);
    CHECK(b.prog.seconds[0] == 61);

    // 手动停止：记入已临秒数。
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    yz_book_tick(&b, 10500);
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(!b.timing && b.prog.seconds[0] == 71 && b.toast == YZ_TOAST_TIMER_OFF);

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
    const yz_book_info_t *d = &YZ_BOOKS[0];
    const int last = d->first_entry + d->entry_count - 1;
    b.prog.fav[0] = 0x05;                               // 第 0、2 字
    b.prog.fav[last / 8] |= (uint8_t)(1u << (last % 8));
    const int other = YZ_BOOKS[1].first_entry + 3;      // 另一本帖的收藏不出现在本帖
    b.prog.fav[other / 8] |= (uint8_t)(1u << (other % 8));
    CHECK(yz_book_fav_count(&b, 0) == 3 && yz_book_fav_count(&b, 1) == 1);
    open_home_item(YZ_HOME_FAVORITES);
    CHECK(b.screen == YZ_SCR_CATALOG && b.tab == yz_book_fav_tab(&b) && b.list_len == 3);
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

static void test_library(void) {
    fresh();
    const yz_book_info_t *q = &YZ_BOOKS[1];

    // 字帖目录：上下循环选择，长按返回不换帖。
    open_home_item(YZ_HOME_LIBRARY);
    CHECK(b.screen == YZ_SCR_LIBRARY && b.lib_sel == 0);
    CHECK(key(YZ_BTN_UP, YZ_EV_CLICK) == (YZ_FX_REFRESH | YZ_FX_GLYPH));
    CHECK(b.lib_sel == YZ_BOOK_COUNT - 1);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(b.lib_sel == 0);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) & YZ_FX_SCREEN);
    CHECK(b.screen == YZ_SCR_HOME && b.prog.book == 0);

    // 打开第二本：保存进度，主页回到“继续临帖”，从该帖第一个字开始。
    open_home_item(YZ_HOME_LIBRARY);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    const uint32_t fx = key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_SCREEN && fx & YZ_FX_SAVE_PROG);
    CHECK(b.prog.book == 1 && b.home_sel == YZ_HOME_CONTINUE);
    open_home_item(YZ_HOME_CONTINUE);
    CHECK(yz_book_entry(&b) == q->first_entry && b.tab == 0);

    // 本帖内翻页首尾相接，不会跑到第一本。
    key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(yz_book_entry(&b) == q->first_entry + q->entry_count - 1);
    CHECK(b.tab == q->chapter_count - 1);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    const int q_cur = yz_book_entry(&b);
    CHECK(q_cur == q->first_entry + 2 && b.prog.current[1] == q_cur);
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);                     // 记一遍：记在第二本
    CHECK(b.prog.sessions[1] == 1 && b.prog.sessions[0] == 0);
    CHECK(yz_book_done_count(&b, 1) == 1 && yz_book_done_count(&b, 0) == 0);

    // 换回第一本，再换回第二本：各自记住临到的字。
    key(YZ_BTN_OK, YZ_EV_LONG);
    key(YZ_BTN_OK, YZ_EV_LONG);
    b.prog.current[0] = 40;
    open_book(0);
    open_home_item(YZ_HOME_CONTINUE);
    CHECK(yz_book_entry(&b) == 40);
    key(YZ_BTN_OK, YZ_EV_LONG);
    key(YZ_BTN_OK, YZ_EV_LONG);
    open_book(1);
    open_home_item(YZ_HOME_CATALOG);
    CHECK(yz_book_entry(&b) == q_cur);
    CHECK(yz_book_tab_count(&b) == q->chapter_count + 1);
    key(YZ_BTN_OK, YZ_EV_LONG);

    // 再次打开当前已打开的字帖：不算改动，不触发保存。
    open_home_item(YZ_HOME_LIBRARY);
    CHECK(b.lib_sel == 1);
    CHECK(!(key(YZ_BTN_OK, YZ_EV_CLICK) & YZ_FX_SAVE_PROG));
}

static void test_settings(void) {
    fresh();
    // 两本帖都有进度；清除只作用于当前打开的第一本。
    const int q0 = YZ_BOOKS[1].first_entry;
    b.prog.count[q0] = 4;
    b.prog.fav[q0 / 8] |= (uint8_t)(1u << (q0 % 8));
    b.prog.sessions[1] = 9;
    b.prog.current[1] = (uint16_t)(q0 + 5);

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
    b.prog.current[0] = 40;
    b.prog.sessions[0] = 7;
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
    CHECK(b.prog.count[5] == 0 && (b.prog.fav[0] & 1) == 0 && b.prog.current[0] == 0 && b.prog.sessions[0] == 0);
    // 第二本帖与设置都不受影响。
    CHECK(b.prog.count[q0] == 4 && yz_book_is_fav(&b, q0) && b.prog.sessions[1] == 9);
    CHECK(b.prog.current[1] == q0 + 5);
    CHECK(b.cfg.grid == YZ_GRID_TIAN && b.cfg.bright_idx == 3);
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) & YZ_FX_SCREEN);
    CHECK(b.screen == YZ_SCR_HOME);
}

static void test_init_sanitizes(void) {
    yz_cfg_t cfg;
    yz_progress_t prog;
    yz_cfg_default(&cfg);
    yz_progress_reset(&prog);
    for (int k = 0; k < YZ_BOOK_COUNT; k++) CHECK(prog.current[k] == YZ_BOOKS[k].first_entry);

    // 字帖下标越界、记住的字不属于该帖：都退回安全值。
    prog.book = YZ_BOOK_COUNT;
    prog.current[0] = YZ_ENTRY_COUNT + 5;
    prog.current[1] = 3;                                // 第一本的字，不属于第二本
    yz_book_init(&b, &cfg, &prog);
    CHECK(b.prog.book == 0 && b.prog.current[0] == 0 && yz_book_entry(&b) == 0);
    CHECK(b.prog.current[1] == YZ_BOOKS[1].first_entry);
    cfg.timer_idx = 99;
    CHECK(yz_book_timer_total_ms(&cfg) == 60000);

    // 打开第二本帖启动：停在它记住的字所在章节。
    yz_cfg_default(&cfg);
    yz_progress_reset(&prog);
    prog.book = 1;
    prog.current[1] = (uint16_t)(YZ_BOOKS[1].first_entry + 30);
    yz_book_init(&b, &cfg, &prog);
    CHECK(b.lib_sel == 1 && yz_book_entry(&b) == YZ_BOOKS[1].first_entry + 30);
}

int main(void) {
    test_catalog_integrity();
    test_home_and_back();
    test_catalog_navigation();
    test_practice_keys();
    test_timer();
    test_favorites_snapshot();
    test_library();
    test_settings();
    test_init_sanitizes();
    printf("test_yz_book: PASS\n");
    return 0;
}
