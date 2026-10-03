// tests/test_yz_book.c —— 字帖状态机：字目完整性、页面跳转、目录与临帖按键、碑文卷 / 分类卷各自
// 首尾相接、收藏快照、计时临写、分卷目录跳到未临的字、设置与清除进度。
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

static int chapter_index(const char *name) {
    for (int c = 0; c < YZ_CHAPTER_COUNT; c++) {
        if (strcmp(YZ_CHAPTERS[c].name, name) == 0) return c;
    }
    return -1;
}

// 第 k 卷第一个字在字目中的下标（碑文卷首尾相接）。
static int text_start(int chapter) {
    return yz_chapter_entry(chapter, 0);
}

static void test_catalog_integrity(void) {
    // 全碑 2025 字（含偈颂后“其一”至“其七”十四个小字），从“大唐西京”到“史華刻”。
    CHECK(YZ_ENTRY_COUNT == 2025);
    const char *head = "大唐西京千福寺多寶佛塔感應碑文";
    for (int i = 0; head[0]; i++) {
        const size_t n = strlen(YZ_ENTRIES[i].trad);
        CHECK(strncmp(head, YZ_ENTRIES[i].trad, n) == 0);
        head += n;
    }
    CHECK(strcmp(YZ_ENTRIES[YZ_ENTRY_COUNT - 1].trad, "刻") == 0);
    CHECK(strcmp(YZ_ENTRIES[YZ_ENTRY_COUNT - 3].trad, "史") == 0);

    // 九个碑文卷按字目顺序首尾相接、覆盖全碑；之后是五个分类卷，卷表与条目表首尾相接。
    CHECK(YZ_TEXT_CHAPTER_COUNT == 9 && YZ_CHAPTER_COUNT == 14);
    static const char *const NAMES[YZ_CHAPTER_COUNT] = {
        "篇首", "出家", "建塔", "赐额", "舍利", "塔相", "法华", "偈颂", "题记",
        "数目", "独体", "左右", "上下", "包围",
    };
    int next_entry = 0, next_item = 0;
    for (int c = 0; c < YZ_CHAPTER_COUNT; c++) {
        const yz_chapter_t *ch = &YZ_CHAPTERS[c];
        CHECK(strcmp(ch->name, NAMES[c]) == 0);
        CHECK(ch->first == next_item && ch->count > 0 && ch->count <= YZ_CHAPTER_MAX_ITEMS);
        CHECK(ch->cols == 5 && ch->cols <= YZ_CATALOG_MAX_COLS);
        CHECK(ch->text == (c < YZ_TEXT_CHAPTER_COUNT));
        if (ch->text) {
            for (int i = 0; i < ch->count; i++) {
                CHECK(yz_chapter_entry(c, i) == next_entry + i);
                int pos = -1;
                CHECK(yz_catalog_chapter_of(next_entry + i, &pos) == c && pos == i);
            }
            next_entry += ch->count;
        }
        next_item += ch->count;
    }
    CHECK(next_entry == YZ_ENTRY_COUNT && next_item == YZ_CHAPTER_ITEM_COUNT);
    CHECK(yz_catalog_chapter_of(-1, NULL) == -1 && yz_catalog_chapter_of(YZ_ENTRY_COUNT, NULL) == -1);

    // 分类卷：每个不同的字恰好收一处，同卷不重复；结构与分类相符；数目卷按数值排列。
    static uint8_t seen_char[YZ_ENTRY_COUNT];
    int distinct = 0;
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) {
        int first = i;
        for (int j = 0; j < i; j++) {
            if (strcmp(YZ_ENTRIES[j].trad, YZ_ENTRIES[i].trad) == 0) {
                first = j;
                break;
            }
        }
        if (first == i) distinct++;
        seen_char[i] = (uint8_t)(first == i);          // 1 = 这个字第一次出现
    }
    int cat_items = 0;
    for (int c = YZ_TEXT_CHAPTER_COUNT; c < YZ_CHAPTER_COUNT; c++) {
        for (int i = 0; i < YZ_CHAPTERS[c].count; i++) {
            const int e = yz_chapter_entry(c, i);
            CHECK(e >= 0 && e < YZ_ENTRY_COUNT);
            for (int k = YZ_TEXT_CHAPTER_COUNT; k < YZ_CHAPTER_COUNT; k++) {
                for (int j = 0; j < YZ_CHAPTERS[k].count; j++) {
                    if (k == c && j == i) continue;
                    CHECK(strcmp(YZ_ENTRIES[yz_chapter_entry(k, j)].trad, YZ_ENTRIES[e].trad) != 0);
                }
            }
            if (c > YZ_TEXT_CHAPTER_COUNT) CHECK(YZ_ENTRIES[e].structure == c - YZ_TEXT_CHAPTER_COUNT - 1);
            cat_items++;
        }
    }
    CHECK(distinct == 841 && cat_items == distinct);
    const char *nums = "一二三四五六七八九十廿百千萬";
    const int num = chapter_index("数目");
    for (int i = 0; i < YZ_CHAPTERS[num].count; i++) {
        const char *t = YZ_ENTRIES[yz_chapter_entry(num, i)].trad;
        CHECK(strncmp(nums, t, strlen(t)) == 0);
        nums += strlen(t);
    }
    CHECK(nums[0] == '\0');

    for (int i = 0; i < YZ_ENTRY_COUNT; i++) {
        const yz_entry_t *e = &YZ_ENTRIES[i];
        CHECK(e->structure < YZ_STRUCT_COUNT && e->focus < YZ_FOCUS_COUNT);
        CHECK(strstr(e->phrase, e->trad) != NULL);   // 碑文语境里一定有本字
        CHECK(e->simp[0] && e->pinyin[0] && e->source[0]);
    }
    CHECK(YZ_BOOK.name[0] && YZ_BOOK.author[0] && YZ_BOOK.phrase_tag[0] && YZ_BOOK.intro[0]);
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

    // 继续临帖：进入当前字所在的碑文卷。
    b.prog.current = 600;
    open_home_item(YZ_HOME_CONTINUE);
    CHECK(b.screen == YZ_SCR_PRACTICE);
    CHECK(yz_book_entry(&b) == 600);
    CHECK(b.tab == chapter_index("赐额") && b.pos == 600 - text_start(b.tab));
}

static void test_catalog_navigation(void) {
    fresh();
    const int last_text = YZ_TEXT_CHAPTER_COUNT - 1;
    open_home_item(YZ_HOME_CATALOG);
    CHECK(b.screen == YZ_SCR_CATALOG && b.tab == 0 && b.pos == 0);
    CHECK(yz_book_cols(&b) == 5);

    // 上移越过全碑第一字：回到题记最后一字（碑文卷首尾相接，不进分类卷）。
    uint32_t fx = key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_SCREEN);
    CHECK(yz_book_entry(&b) == YZ_ENTRY_COUNT - 1 && b.tab == last_text);
    CHECK(b.pos / 5 >= b.first_row && b.pos / 5 < b.first_row + YZ_CATALOG_ROWS);

    // 下移越过最后一字：回到篇首第一字。
    fx = key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(yz_book_entry(&b) == 0 && b.tab == 0 && (fx & YZ_FX_SCREEN));

    // 卷内移动只刷新，不重建页面；双击跳一行；跨过卷尾把余数带进下一卷。
    fx = key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(fx == (YZ_FX_REFRESH | YZ_FX_GLYPH));
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(yz_book_entry(&b) == 6);
    const int head_len = YZ_CHAPTERS[0].count;           // 篇首 88 字
    while (yz_book_entry(&b) + 5 < head_len) key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    const int before = yz_book_entry(&b);
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(yz_book_entry(&b) == before + 5 && b.tab == 1 && b.pos == before + 5 - head_len);

    // 长按换卷：各卷与收藏页签一起首尾循环。
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.tab == 2 && b.pos == 0 && b.first_row == 0);
    while (b.tab != YZ_FAV_TAB) key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.list_len == 0 && yz_book_entry(&b) == -1);
    CHECK(key(YZ_BTN_OK, YZ_EV_CLICK) == 0);            // 空收藏不能进入临帖
    CHECK(key(YZ_BTN_DOWN, YZ_EV_CLICK) == (YZ_FX_REFRESH | YZ_FX_GLYPH));
    key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.tab == 0);
    key(YZ_BTN_UP, YZ_EV_LONG);
    CHECK(b.tab == YZ_FAV_TAB);

    // 分类卷之间首尾相接：包围最后一字再往下是数目第一字，反之亦然。
    const int num = chapter_index("数目"), en = chapter_index("包围");
    while (b.tab != en) key(YZ_BTN_UP, YZ_EV_LONG);
    while (b.pos != b.list_len - 1) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(b.tab == num && b.pos == 0 && strcmp(YZ_ENTRIES[yz_book_entry(&b)].trad, "一") == 0);
    key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(b.tab == en && b.pos == YZ_CHAPTERS[en].count - 1);

    // 滚动：在最长的碑文卷里一直下移，选中行始终可见。
    const int edict = chapter_index("赐额");
    while (b.tab != edict) key(YZ_BTN_DOWN, YZ_EV_LONG);
    CHECK(b.list_len == YZ_CHAPTERS[edict].count);
    for (int i = 0; i < b.list_len - 1; i++) {
        key(YZ_BTN_DOWN, YZ_EV_CLICK);
        const int row = b.pos / yz_book_cols(&b);
        CHECK(row >= b.first_row && row < b.first_row + YZ_CATALOG_ROWS);
    }
    CHECK(b.pos == b.list_len - 1 && b.first_row == (b.list_len - 1) / 5 - YZ_CATALOG_ROWS + 1);
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

    // 记一遍：计数饱和于 255，累计遍数照常增加。
    key(YZ_BTN_DOWN, YZ_EV_DOUBLE);
    CHECK(b.prog.count[e0] == 1 && b.prog.sessions == 1 && b.toast == YZ_TOAST_MARKED);
    CHECK(yz_book_done_count(&b) == 1 && yz_book_chapter_done(&b, 0) == 1 && yz_book_chapter_done(&b, 1) == 0);
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

    // 从分类卷进入临帖：记住的字回主页“继续临帖”时定位回它所在的碑文卷。
    const int single = chapter_index("独体");
    while (b.tab != single) key(YZ_BTN_DOWN, YZ_EV_LONG);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    const int picked = yz_book_entry(&b);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.screen == YZ_SCR_PRACTICE && b.prog.current == picked);
    key(YZ_BTN_OK, YZ_EV_LONG);
    key(YZ_BTN_OK, YZ_EV_LONG);
    open_home_item(YZ_HOME_CONTINUE);
    CHECK(yz_book_entry(&b) == picked && b.tab == yz_catalog_chapter_of(picked, NULL));
    CHECK(b.tab < YZ_TEXT_CHAPTER_COUNT);
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
    const int last = YZ_ENTRY_COUNT - 1;
    b.prog.fav[0] = 0x05;                               // 第 0、2 字
    b.prog.fav[last / 8] |= (uint8_t)(1u << (last % 8));
    CHECK(yz_book_fav_count(&b) == 3);
    open_home_item(YZ_HOME_FAVORITES);
    CHECK(b.screen == YZ_SCR_CATALOG && b.tab == YZ_FAV_TAB && b.list_len == 3);
    CHECK(yz_book_cols(&b) == YZ_FAV_COLS);
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

    // 收藏全碑每一个字：列表容得下。
    memset(b.prog.fav, 0xFF, sizeof b.prog.fav);
    b.prog.fav[YZ_FAV_BYTES - 1] = (uint8_t)((1u << (YZ_ENTRY_COUNT % 8 ? YZ_ENTRY_COUNT % 8 : 8)) - 1);
    key(YZ_BTN_OK, YZ_EV_LONG);
    open_home_item(YZ_HOME_FAVORITES);
    CHECK(b.list_len == YZ_ENTRY_COUNT && yz_book_fav_count(&b) == YZ_ENTRY_COUNT);
}

static void test_volumes(void) {
    fresh();
    // 从主页进入：选中当前字所在的碑文卷。
    b.prog.current = 1000;
    const int relic = chapter_index("舍利");
    open_home_item(YZ_HOME_VOLUMES);
    CHECK(b.screen == YZ_SCR_VOLUMES && b.vol_sel == relic);

    // 上 / 下在 14 卷之间循环（收藏不在此列）。
    while (b.vol_sel != 0) key(YZ_BTN_UP, YZ_EV_CLICK);
    CHECK(key(YZ_BTN_UP, YZ_EV_CLICK) == (YZ_FX_REFRESH | YZ_FX_GLYPH));
    CHECK(b.vol_sel == YZ_CHAPTER_COUNT - 1);
    key(YZ_BTN_DOWN, YZ_EV_CLICK);
    CHECK(b.vol_sel == 0);
    CHECK(key(YZ_BTN_OK, YZ_EV_DOUBLE) == 0);

    // 打开一卷：停在第一个还没临过的字。
    const int build = chapter_index("建塔");
    const int first = text_start(build);
    for (int i = 0; i < 7; i++) b.prog.count[first + i] = 1;
    b.prog.count[first + 9] = 2;                        // 跳着临过的字不影响“第一个未临”
    while (b.vol_sel != build) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    uint32_t fx = key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(fx & YZ_FX_SCREEN && b.screen == YZ_SCR_CATALOG);
    CHECK(b.tab == build && b.pos == 7 && yz_book_entry(&b) == first + 7);
    CHECK(yz_book_chapter_done(&b, build) == 8);

    // 全卷都临过：停在卷首。数目卷是分类卷，同样适用。
    key(YZ_BTN_OK, YZ_EV_LONG);
    const int num = chapter_index("数目");
    for (int i = 0; i < YZ_CHAPTERS[num].count; i++) b.prog.count[yz_chapter_entry(num, i)] = 1;
    open_home_item(YZ_HOME_VOLUMES);
    while (b.vol_sel != num) key(YZ_BTN_DOWN, YZ_EV_CLICK);
    key(YZ_BTN_OK, YZ_EV_CLICK);
    CHECK(b.tab == num && b.pos == 0);
    CHECK(yz_book_chapter_done(&b, num) == YZ_CHAPTERS[num].count);

    // 长按确定返回主页。
    key(YZ_BTN_OK, YZ_EV_LONG);
    open_home_item(YZ_HOME_VOLUMES);
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) & YZ_FX_SCREEN);
    CHECK(b.screen == YZ_SCR_HOME);
    CHECK(yz_book_chapter_done(&b, -1) == 0 && yz_book_chapter_done(&b, YZ_CHAPTER_COUNT) == 0);
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
    b.prog.count[YZ_ENTRY_COUNT - 1] = 9;
    b.prog.fav[0] = 1;
    b.prog.current = 1500;
    b.prog.sessions = 7;
    b.prog.seconds = 300;
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
    CHECK(b.prog.count[5] == 0 && b.prog.count[YZ_ENTRY_COUNT - 1] == 0 && (b.prog.fav[0] & 1) == 0);
    CHECK(b.prog.current == 0 && b.prog.sessions == 0 && b.prog.seconds == 0);
    CHECK(yz_book_done_count(&b) == 0 && b.tab == 0 && b.pos == 0);
    // 设置不受影响。
    CHECK(b.cfg.grid == YZ_GRID_TIAN && b.cfg.bright_idx == 3);
    CHECK(key(YZ_BTN_OK, YZ_EV_LONG) & YZ_FX_SCREEN);
    CHECK(b.screen == YZ_SCR_HOME);
}

static void test_init_sanitizes(void) {
    yz_cfg_t cfg;
    yz_progress_t prog;
    yz_cfg_default(&cfg);
    yz_progress_reset(&prog);
    CHECK(prog.current == 0 && prog.sessions == 0);

    // 记住的字越界：退回全碑第一个字。
    prog.current = YZ_ENTRY_COUNT + 5;
    yz_book_init(&b, &cfg, &prog);
    CHECK(b.prog.current == 0 && yz_book_entry(&b) == 0 && b.tab == 0);
    cfg.timer_idx = 99;
    CHECK(yz_book_timer_total_ms(&cfg) == 60000);

    // 启动时停在记住的字所在的碑文卷。
    yz_cfg_default(&cfg);
    prog.current = YZ_ENTRY_COUNT - 1;
    yz_book_init(&b, &cfg, &prog);
    CHECK(yz_book_entry(&b) == YZ_ENTRY_COUNT - 1 && b.tab == YZ_TEXT_CHAPTER_COUNT - 1);
}

int main(void) {
    test_catalog_integrity();
    test_home_and_back();
    test_catalog_navigation();
    test_practice_keys();
    test_timer();
    test_favorites_snapshot();
    test_volumes();
    test_settings();
    test_init_sanitizes();
    printf("test_yz_book: PASS\n");
    return 0;
}
