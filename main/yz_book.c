// main/yz_book.c —— 字帖应用状态机，按键语义见 yz_book_input() 各分支的注释。
#include "yz_book.h"

#include <string.h>

const uint16_t YZ_TIMER_SECONDS[YZ_TIMER_OPTIONS] = { 30, 60, 90, 120, 180, 300 };
const uint8_t YZ_BRIGHT_PERCENT[YZ_BRIGHT_OPTIONS] = { 30, 55, 80, 100 };

void yz_cfg_default(yz_cfg_t *cfg) {
    cfg->grid = YZ_GRID_MI;
    cfg->ink = YZ_INK_STONE;
    cfg->timer_idx = 1;          // 60 秒
    cfg->auto_next = 1;
    cfg->sound = 1;
    cfg->bright_idx = 2;         // 80%
}

void yz_progress_reset(yz_progress_t *prog) {
    memset(prog, 0, sizeof(*prog));
}

uint32_t yz_book_timer_total_ms(const yz_cfg_t *cfg) {
    const uint8_t idx = cfg->timer_idx < YZ_TIMER_OPTIONS ? cfg->timer_idx : 1;
    return (uint32_t)YZ_TIMER_SECONDS[idx] * 1000u;
}

bool yz_book_is_fav(const yz_book_t *book, int entry) {
    if (entry < 0 || entry >= YZ_ENTRY_COUNT) return false;
    return (book->prog.fav[entry / 8] >> (entry % 8)) & 1u;
}

static void set_fav(yz_book_t *book, int entry, bool on) {
    const uint8_t bit = (uint8_t)(1u << (entry % 8));
    if (on) book->prog.fav[entry / 8] |= bit;
    else book->prog.fav[entry / 8] &= (uint8_t)~bit;
}

uint16_t yz_book_fav_count(const yz_book_t *book) {
    uint16_t n = 0;
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) n += yz_book_is_fav(book, i);
    return n;
}

uint16_t yz_book_done_count(const yz_book_t *book) {
    uint16_t n = 0;
    for (int i = 0; i < YZ_ENTRY_COUNT; i++) n += book->prog.count[i] > 0;
    return n;
}

uint16_t yz_book_chapter_done(const yz_book_t *book, int chapter) {
    if (chapter < 0 || chapter >= YZ_CHAPTER_COUNT) return 0;
    uint16_t n = 0;
    for (int i = 0; i < YZ_CHAPTERS[chapter].count; i++) n += book->prog.count[yz_chapter_entry(chapter, i)] > 0;
    return n;
}

uint8_t yz_book_cols(const yz_book_t *book) {
    if (book->tab >= YZ_FAV_TAB) return YZ_FAV_COLS;
    return YZ_CHAPTERS[book->tab].cols;
}

int yz_book_entry(const yz_book_t *book) {
    if (book->list_len == 0 || book->pos >= book->list_len) return -1;
    return book->list[book->pos];
}

// ---- 浏览列表 ----

static void load_tab(yz_book_t *book, uint8_t tab) {
    book->tab = tab;
    book->list_len = 0;
    if (tab == YZ_FAV_TAB) {
        for (int i = 0; i < YZ_ENTRY_COUNT; i++) {
            if (yz_book_is_fav(book, i)) book->list[book->list_len++] = (uint16_t)i;
        }
    } else {
        for (int i = 0; i < YZ_CHAPTERS[tab].count; i++) {
            book->list[book->list_len++] = (uint16_t)yz_chapter_entry(tab, i);
        }
    }
    book->pos = 0;
    book->first_row = 0;
}

static void keep_row_visible(yz_book_t *book) {
    const uint16_t row = book->pos / yz_book_cols(book);
    if (row < book->first_row) book->first_row = row;
    if (row >= book->first_row + YZ_CATALOG_ROWS) book->first_row = (uint16_t)(row - YZ_CATALOG_ROWS + 1);
}

// 定位到某个字所在的碑文卷；越界的字退回全碑第一个字。
static void locate(yz_book_t *book, int entry) {
    int pos = 0;
    int chapter = yz_catalog_chapter_of(entry, &pos);
    if (chapter < 0) {
        chapter = 0;
        pos = 0;
    }
    load_tab(book, (uint8_t)chapter);
    book->pos = (uint16_t)pos;
    keep_row_visible(book);
}

// 卷所在的组：碑文卷一组，分类卷一组。翻字越过卷尾时只在本组内首尾相接——
// 通临到题记最后一字再往下，回到篇首，而不是跑进分类卷。
static void group_of(int tab, int *first, int *count) {
    if (tab < YZ_TEXT_CHAPTER_COUNT) {
        *first = 0;
        *count = YZ_TEXT_CHAPTER_COUNT;
    } else {
        *first = YZ_TEXT_CHAPTER_COUNT;
        *count = YZ_CHAPTER_COUNT - YZ_TEXT_CHAPTER_COUNT;
    }
}

// 前后移动 delta 个字。越过卷尾时把余数带进本组相邻的卷；收藏页签在快照内循环。
static void step(yz_book_t *book, int delta) {
    if (book->list_len == 0) return;
    if (book->tab == YZ_FAV_TAB) {
        const int n = book->list_len;
        book->pos = (uint16_t)(((book->pos + delta) % n + n) % n);
    } else {
        int first, count;
        group_of(book->tab, &first, &count);
        int tab = book->tab;
        int pos = book->pos + delta;
        for (;;) {
            const int len = YZ_CHAPTERS[tab].count;
            if (pos >= len) {
                pos -= len;
                tab = first + (tab - first + 1) % count;
            } else if (pos < 0) {
                tab = first + (tab - first + count - 1) % count;
                pos += YZ_CHAPTERS[tab].count;
            } else {
                break;
            }
        }
        if (tab != book->tab) load_tab(book, (uint8_t)tab);
        book->pos = (uint16_t)pos;
    }
    keep_row_visible(book);
}

// 切换页签：各卷与收藏页签一起循环。
static void switch_tab(yz_book_t *book, int delta) {
    load_tab(book, (uint8_t)(((book->tab + delta) % YZ_TAB_COUNT + YZ_TAB_COUNT) % YZ_TAB_COUNT));
}

// ---- 提示条与计时 ----

static uint32_t toast(yz_book_t *book, yz_toast_t t) {
    book->toast = t;
    book->toast_left_ms = YZ_TOAST_MS;
    return YZ_FX_TOAST;
}

static void bump_count(yz_book_t *book, int entry) {
    if (book->prog.count[entry] < 255) book->prog.count[entry]++;
    book->prog.sessions++;
}

static void timer_start(yz_book_t *book) {
    book->timing = true;
    book->timer_total_ms = yz_book_timer_total_ms(&book->cfg);
    book->timer_left_ms = book->timer_total_ms;
}

static void timer_stop(yz_book_t *book) {
    if (book->timing) {
        book->prog.seconds += (book->timer_total_ms - book->timer_left_ms) / 1000u;
    }
    book->timing = false;
    book->timer_left_ms = 0;
}

// 临帖页换字：记住当前字，计时中则为新字重新计时。
static uint32_t practice_moved(yz_book_t *book) {
    book->prog.current = (uint16_t)yz_book_entry(book);
    if (book->timing) {
        timer_stop(book);    // 记下已临的时长，再为新字重新计时
        timer_start(book);
    }
    return YZ_FX_GLYPH | YZ_FX_REFRESH | YZ_FX_SAVE_PROG;
}

static uint32_t enter_practice(yz_book_t *book) {
    if (yz_book_entry(book) < 0) return 0;
    book->screen = YZ_SCR_PRACTICE;
    book->card = false;
    book->prog.current = (uint16_t)yz_book_entry(book);
    return YZ_FX_SCREEN | YZ_FX_GLYPH | YZ_FX_SAVE_PROG;
}

static uint32_t go_home(yz_book_t *book) {
    timer_stop(book);
    book->screen = YZ_SCR_HOME;
    book->card = false;
    book->confirm_reset = false;
    return YZ_FX_SCREEN | YZ_FX_GLYPH;
}

void yz_book_init(yz_book_t *book, const yz_cfg_t *cfg, const yz_progress_t *prog) {
    memset(book, 0, sizeof(*book));
    book->cfg = *cfg;
    book->prog = *prog;
    if (book->prog.current >= YZ_ENTRY_COUNT) book->prog.current = 0;
    locate(book, book->prog.current);
    book->screen = YZ_SCR_HOME;
}

// ---- 各页面的按键 ----

static uint32_t home_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev) {
    if (ev != YZ_EV_CLICK) return 0;
    if (btn == YZ_BTN_UP || btn == YZ_BTN_DOWN) {
        const int d = btn == YZ_BTN_UP ? -1 : 1;
        book->home_sel = (uint8_t)((book->home_sel + d + YZ_HOME_COUNT) % YZ_HOME_COUNT);
        return YZ_FX_REFRESH;
    }
    switch ((yz_home_item_t)book->home_sel) {
    case YZ_HOME_CONTINUE:
        locate(book, book->prog.current);
        return enter_practice(book);
    case YZ_HOME_CATALOG:
        locate(book, book->prog.current);
        book->screen = YZ_SCR_CATALOG;
        return YZ_FX_SCREEN | YZ_FX_GLYPH;
    case YZ_HOME_VOLUMES: {
        const int chapter = yz_catalog_chapter_of(book->prog.current, NULL);
        book->vol_sel = (uint8_t)(chapter < 0 ? 0 : chapter);
        book->screen = YZ_SCR_VOLUMES;
        return YZ_FX_SCREEN | YZ_FX_GLYPH;
    }
    case YZ_HOME_FAVORITES:
        load_tab(book, YZ_FAV_TAB);
        book->screen = YZ_SCR_CATALOG;
        return YZ_FX_SCREEN | YZ_FX_GLYPH;
    case YZ_HOME_ABOUT:
        book->about_page = 0;
        book->screen = YZ_SCR_ABOUT;
        return YZ_FX_SCREEN;
    case YZ_HOME_SETTINGS:
    default:
        book->set_sel = 0;
        book->confirm_reset = false;
        book->screen = YZ_SCR_SETTINGS;
        return YZ_FX_SCREEN;
    }
}

// 目录：单击 ±1 字，双击 ±1 行，长按上 / 下换卷，确定单击开始临帖，确定长按回主页。
static uint32_t catalog_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev) {
    if (btn == YZ_BTN_OK) {
        if (ev == YZ_EV_CLICK) return enter_practice(book);
        if (ev == YZ_EV_LONG) return go_home(book);
        return 0;
    }
    const int dir = btn == YZ_BTN_UP ? -1 : 1;
    const uint8_t old_tab = book->tab;
    if (ev == YZ_EV_CLICK) step(book, dir);
    else if (ev == YZ_EV_DOUBLE) step(book, dir * yz_book_cols(book));
    else if (ev == YZ_EV_LONG) switch_tab(book, dir);
    else return 0;
    return (book->tab != old_tab ? YZ_FX_SCREEN : YZ_FX_REFRESH) | YZ_FX_GLYPH;
}

// 临帖：上 / 下单击换字；确定单击笔法卡，双击收藏，长按回目录；
//       上长按换格线，上双击换底色；下长按开始 / 停止计时，下双击记一遍。
static uint32_t practice_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev) {
    const int entry = yz_book_entry(book);
    if (entry < 0) return 0;
    if (btn == YZ_BTN_OK) {
        switch (ev) {
        case YZ_EV_CLICK:
            book->card = !book->card;
            return YZ_FX_REFRESH;
        case YZ_EV_DOUBLE: {
            const bool on = !yz_book_is_fav(book, entry);
            set_fav(book, entry, on);
            return YZ_FX_REFRESH | YZ_FX_SAVE_PROG | toast(book, on ? YZ_TOAST_FAV_ON : YZ_TOAST_FAV_OFF);
        }
        case YZ_EV_LONG:
            timer_stop(book);
            book->card = false;
            book->screen = YZ_SCR_CATALOG;
            keep_row_visible(book);
            return YZ_FX_SCREEN | YZ_FX_GLYPH;
        default:
            return 0;
        }
    }
    if (ev == YZ_EV_CLICK) {
        step(book, btn == YZ_BTN_UP ? -1 : 1);
        return practice_moved(book);
    }
    if (btn == YZ_BTN_UP) {
        if (ev == YZ_EV_LONG) {
            book->cfg.grid = (uint8_t)((book->cfg.grid + 1) % YZ_GRID_COUNT);
            return YZ_FX_REFRESH | YZ_FX_SAVE_CFG | toast(book, YZ_TOAST_GRID);
        }
        if (ev == YZ_EV_DOUBLE) {
            book->cfg.ink = (uint8_t)((book->cfg.ink + 1) % YZ_INK_COUNT);
            return YZ_FX_REFRESH | YZ_FX_SAVE_CFG | toast(book, YZ_TOAST_INK);
        }
        return 0;
    }
    if (ev == YZ_EV_LONG) {
        if (book->timing) {
            timer_stop(book);
            return YZ_FX_REFRESH | YZ_FX_SAVE_PROG | toast(book, YZ_TOAST_TIMER_OFF);
        }
        timer_start(book);
        book->card = false;
        return YZ_FX_REFRESH | toast(book, YZ_TOAST_TIMER_ON);
    }
    if (ev == YZ_EV_DOUBLE) {
        bump_count(book, entry);
        return YZ_FX_REFRESH | YZ_FX_SAVE_PROG | toast(book, YZ_TOAST_MARKED);
    }
    return 0;
}

// 分卷目录：上 / 下选卷，确定打开该卷目录并停在第一个还没临过的字（全卷都临过则停在卷首），
// 长按确定返回主页。
static uint32_t volumes_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev) {
    if (btn == YZ_BTN_OK && ev == YZ_EV_LONG) return go_home(book);
    if (ev != YZ_EV_CLICK) return 0;
    if (btn == YZ_BTN_OK) {
        load_tab(book, book->vol_sel);
        for (uint16_t i = 0; i < book->list_len; i++) {
            if (book->prog.count[book->list[i]] == 0) {
                book->pos = i;
                break;
            }
        }
        keep_row_visible(book);
        book->screen = YZ_SCR_CATALOG;
        return YZ_FX_SCREEN | YZ_FX_GLYPH;
    }
    const int d = btn == YZ_BTN_UP ? -1 : 1;
    book->vol_sel = (uint8_t)((book->vol_sel + d + YZ_CHAPTER_COUNT) % YZ_CHAPTER_COUNT);
    return YZ_FX_REFRESH | YZ_FX_GLYPH;
}

static uint32_t about_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev) {
    if (btn == YZ_BTN_OK && ev == YZ_EV_LONG) return go_home(book);
    if (ev != YZ_EV_CLICK) return 0;
    const int d = btn == YZ_BTN_UP ? -1 : 1;
    book->about_page = (uint8_t)((book->about_page + d + YZ_ABOUT_PAGES) % YZ_ABOUT_PAGES);
    return YZ_FX_REFRESH;
}

static uint32_t change_setting(yz_book_t *book) {
    yz_cfg_t *c = &book->cfg;
    switch ((yz_setting_t)book->set_sel) {
    case YZ_SET_GRID: c->grid = (uint8_t)((c->grid + 1) % YZ_GRID_COUNT); break;
    case YZ_SET_INK: c->ink = (uint8_t)((c->ink + 1) % YZ_INK_COUNT); break;
    case YZ_SET_TIMER: c->timer_idx = (uint8_t)((c->timer_idx + 1) % YZ_TIMER_OPTIONS); break;
    case YZ_SET_AUTO: c->auto_next = !c->auto_next; break;
    case YZ_SET_SOUND: c->sound = !c->sound; break;
    case YZ_SET_BRIGHT:
        c->bright_idx = (uint8_t)((c->bright_idx + 1) % YZ_BRIGHT_OPTIONS);
        return YZ_FX_REFRESH | YZ_FX_SAVE_CFG | YZ_FX_BRIGHT;
    case YZ_SET_RESET:
    default:
        if (!book->confirm_reset) {
            book->confirm_reset = true;
            return YZ_FX_REFRESH;
        }
        // 清除临写遍数、收藏、累计统计，回到全碑第一个字。
        book->confirm_reset = false;
        yz_progress_reset(&book->prog);
        locate(book, 0);
        return YZ_FX_REFRESH | YZ_FX_SAVE_PROG | toast(book, YZ_TOAST_RESET);
    }
    return YZ_FX_REFRESH | YZ_FX_SAVE_CFG;
}

static uint32_t settings_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev) {
    if (btn == YZ_BTN_OK && ev == YZ_EV_LONG) return go_home(book);
    if (ev != YZ_EV_CLICK) return 0;
    if (btn == YZ_BTN_OK) return change_setting(book);
    const int d = btn == YZ_BTN_UP ? -1 : 1;
    book->set_sel = (uint8_t)((book->set_sel + d + YZ_SET_COUNT) % YZ_SET_COUNT);
    book->confirm_reset = false;
    return YZ_FX_REFRESH;
}

uint32_t yz_book_input(yz_book_t *book, yz_btn_t btn, yz_ev_t ev) {
    if (ev == YZ_EV_PRESS) return 0;     // 只用单击 / 双击 / 长按
    switch (book->screen) {
    case YZ_SCR_HOME: return home_input(book, btn, ev);
    case YZ_SCR_CATALOG: return catalog_input(book, btn, ev);
    case YZ_SCR_PRACTICE: return practice_input(book, btn, ev);
    case YZ_SCR_ABOUT: return about_input(book, btn, ev);
    case YZ_SCR_SETTINGS: return settings_input(book, btn, ev);
    case YZ_SCR_VOLUMES: return volumes_input(book, btn, ev);
    default: return 0;
    }
}

uint32_t yz_book_tick(yz_book_t *book, uint32_t elapsed_ms) {
    uint32_t fx = 0;
    if (book->toast != YZ_TOAST_NONE) {
        if (elapsed_ms >= book->toast_left_ms) {
            book->toast = YZ_TOAST_NONE;
            book->toast_left_ms = 0;
            fx |= YZ_FX_TOAST;
        } else {
            book->toast_left_ms -= elapsed_ms;
        }
    }
    if (!book->timing || book->screen != YZ_SCR_PRACTICE) return fx;

    const uint32_t before_s = (book->timer_left_ms + 999u) / 1000u;
    if (elapsed_ms < book->timer_left_ms) {
        book->timer_left_ms -= elapsed_ms;
        // 只在显示的秒数变化时刷新，避免无谓重绘。
        if ((book->timer_left_ms + 999u) / 1000u != before_s) fx |= YZ_FX_REFRESH;
        return fx;
    }

    // 一遍临写完成。
    const int entry = yz_book_entry(book);
    if (entry >= 0) bump_count(book, entry);
    book->prog.seconds += book->timer_total_ms / 1000u;
    book->timer_left_ms = 0;
    fx |= YZ_FX_REFRESH | YZ_FX_SAVE_PROG | toast(book, YZ_TOAST_TIMER_DONE);
    if (book->cfg.sound) fx |= YZ_FX_CHIME;
    book->timing = false;
    if (book->cfg.auto_next) {
        step(book, 1);
        fx |= practice_moved(book);
        timer_start(book);
    }
    return fx;
}
