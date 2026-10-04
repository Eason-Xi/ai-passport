// main/kj_ui_pages.c —— 限定猜拳各页面的版面（240×320，四角 30 px 圆角遮罩之内）。
#include "kj_ui_internal.h"

#include <stdio.h>
#include <string.h>

#define CARD_ROW_W 64
#define CARD_ROW_H 92

static void no_text(char *buf, size_t n, unsigned no)
{
    snprintf(buf, n, KJ_STR_NO_FMT, no);
}

// ---------------------------------------------------------------------------
// 首页：选择角色
// ---------------------------------------------------------------------------
static void page_title(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, NULL, 0);
    kj_text_at(s, &kj_big48, KJ_C_TEXT, KJ_BIG_TITLE_1, LV_ALIGN_TOP_MID, -50, 34);
    kj_text_at(s, &kj_big48, KJ_C_RED, KJ_BIG_TITLE_2, LV_ALIGN_TOP_MID, 50, 34);
    lv_obj_t *z = kj_text_at(s, &kj_zh14, KJ_C_DIM, KJ_STR_ZAWA, LV_ALIGN_TOP_MID, 0, 94);
    kj_pulse(z, 2400);
    // 三张扇形摆开的牌（中间那张抬高）
    kj_card(s, 38, 128, 56, 78, KJ_ROCK, 0, KJ_CARD_NAME);
    kj_card(s, 92, 116, 56, 78, KJ_SCISSORS, 0, KJ_CARD_NAME);
    kj_card(s, 146, 128, 56, 78, KJ_PAPER, 0, KJ_CARD_NAME);
    kj_pill(s, 22, 222, 196, 32, KJ_STR_ROLE_PLAYER, m->title_sel == 0, KJ_C_RED);
    kj_pill(s, 22, 258, 196, 32, KJ_STR_ROLE_HOST, m->title_sel == 1, KJ_C_RED);
    kj_footer(s, KJ_STR_HINT_TITLE);
}

// ---------------------------------------------------------------------------
// 找赌局
// ---------------------------------------------------------------------------
static const char *phase_text(uint8_t phase)
{
    return phase == KJ_PHASE_RUNNING ? KJ_STR_PHASE_RUN
         : phase == KJ_PHASE_ENDED ? KJ_STR_PHASE_ENDED : KJ_STR_PHASE_LOBBY;
}

static void page_rooms(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, NULL, 0);
    kj_text_at(s, &kj_zh26, KJ_C_TEXT, KJ_STR_ROOMS_TITLE, LV_ALIGN_TOP_MID, 0, 40);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_ROOMS_SUB, LV_ALIGN_TOP_MID, 0, 76);
    if (m->room_count == 0) {
        // 雷达：三圈同心圆 + 跳动的提示
        for (int i = 0; i < 3; i++) {
            int d = 40 + i * 36;
            kj_frame(s, 120 - d / 2, 170 - d / 2, d, d, i == 0 ? KJ_C_RED : 0x3A2F28, 2, LV_RADIUS_CIRCLE);
        }
        kj_box(s, 114, 164, 12, 12, KJ_C_RED, LV_RADIUS_CIRCLE);
        lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_TEXT, KJ_STR_ROOMS_EMPTY, LV_ALIGN_TOP_MID, 0, 238);
        kj_pulse(t, 1600);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_ROOMS_EMPTY2, LV_ALIGN_TOP_MID, 0, 264);
    } else {
        int first = m->room_sel >= 4 ? m->room_sel - 3 : 0;
        for (int i = first; i < m->room_count && i < first + 4; i++) {
            const kj_room_entry_t *r = &m->rooms[i];
            bool sel = i == m->room_sel;
            int y = 102 + (i - first) * 46;
            lv_obj_t *row = kj_box(s, 18, y, 204, 40, sel ? 0x3A1517 : KJ_C_PANEL, 10);
            lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(sel ? KJ_C_RED : KJ_C_LINE), 0);
            char room[5], buf[40];
            snprintf(room, sizeof(room), "%04X", (unsigned)r->room);
            snprintf(buf, sizeof(buf), KJ_STR_ROOM_FMT, room);
            kj_text_at(row, &kj_zh18, KJ_C_TEXT, buf, LV_ALIGN_TOP_LEFT, 12, 1);
            snprintf(buf, sizeof(buf), KJ_STR_ROOM_LINE_FMT, phase_text(r->phase), (unsigned)r->seated);
            kj_text_at(row, &kj_zh14, KJ_C_MUTED, buf, LV_ALIGN_TOP_LEFT, 12, 21);
            kj_signal(row, 170, 13, kj_ui_signal_level(r->rssi), sel ? KJ_C_GOLD : KJ_C_MUTED);
        }
        if (m->joining) {
            lv_obj_t *j = kj_box(s, 50, 196, 140, 44, 0x2B231E, 12);
            lv_obj_set_style_border_width(j, 1, 0);
            lv_obj_set_style_border_color(j, lv_color_hex(KJ_C_GOLD), 0);
            lv_obj_t *t = kj_text(j, &kj_zh18, KJ_C_GOLD, KJ_STR_JOINING);
            lv_obj_center(t);
            kj_pulse(t, 1000);
        }
    }
    kj_footer(s, KJ_STR_HINT_ROOMS);
}

// ---------------------------------------------------------------------------
// 入座等待
// ---------------------------------------------------------------------------
static void room_top_bar(const kj_ui_model_t *m)
{
    char room[5], buf[24];
    snprintf(room, sizeof(room), "%04X", (unsigned)m->room);
    snprintf(buf, sizeof(buf), KJ_STR_ROOM_FMT, room);
    kj_top_bar(kj_ui.scr, buf, KJ_C_MUTED);
}

static void player_top_bar(const kj_ui_model_t *m)
{
    char buf[16];
    no_text(buf, sizeof(buf), m->view.no);
    kj_top_bar(kj_ui.scr, buf, KJ_C_GOLD);
}

static void page_seat(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    room_top_bar(m);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_YOUR_NO, LV_ALIGN_TOP_MID, 0, 50);
    kj_badge(s, 120, 136, 120, m->view.no, KJ_C_RED, true);
    char buf[32];
    snprintf(buf, sizeof(buf), KJ_STR_SEATED_FMT, (unsigned)m->seated);
    kj_text_at(s, &kj_zh18, KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 210);
    bool ended = m->view.phase == KJ_PHASE_ENDED;
    lv_obj_t *t = kj_text_at(s, &kj_zh18, KJ_C_RED, ended ? KJ_STR_WAIT_NEXT : KJ_STR_WAIT_START,
                             LV_ALIGN_TOP_MID, 0, 242);
    kj_pulse(t, 1600);
    kj_footer(s, KJ_STR_HINT_SEAT);
}

// ---------------------------------------------------------------------------
// 手牌主页
// ---------------------------------------------------------------------------
static void card_row(lv_obj_t *s, const kj_view_t *v, int y)
{
    for (int c = 0; c < KJ_CARD_TYPES; c++) {
        uint32_t flags = KJ_CARD_COUNT | (v->cards[c] == 0 ? KJ_CARD_OFF : 0);
        kj_card(s, 18 + c * 70, y, CARD_ROW_W, CARD_ROW_H, (uint8_t)c, v->cards[c], flags);
    }
}

static unsigned hand_total(const kj_view_t *v)
{
    return (unsigned)v->cards[0] + v->cards[1] + v->cards[2];
}

static void page_hand(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    player_top_bar(m);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_STARS_LABEL, LV_ALIGN_TOP_MID, 0, 40);
    kj_stars(s, 56, v->stars, &kj_zh26);
    card_row(s, v, 100);
    char buf[64];
    snprintf(buf, sizeof(buf), KJ_STR_HAND_INFO_FMT, hand_total(v), v->wins, v->losses, v->draws);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, buf, LV_ALIGN_TOP_MID, 0, 204);
    kj_pill(s, 30, 230, 180, 40, KJ_STR_FIND_OPP, true, KJ_C_RED);
    snprintf(buf, sizeof(buf), KJ_STR_AVAIL_FMT, (unsigned)m->opp_count);
    kj_text_at(s, &kj_zh14, m->opp_count ? KJ_C_GOLD : KJ_C_DIM, buf, LV_ALIGN_TOP_MID, 0, 274);
    kj_footer(s, KJ_STR_HINT_HAND);
}

// ---------------------------------------------------------------------------
// 选择对手
// ---------------------------------------------------------------------------
static void page_opponents(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    kj_text_at(s, &kj_zh26, KJ_C_TEXT, KJ_STR_OPP_TITLE, LV_ALIGN_TOP_MID, 0, 40);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_OPP_SUB, LV_ALIGN_TOP_MID, 0, 76);
    if (m->opp_count == 0) {
        kj_text_at(s, &kj_zh18, KJ_C_TEXT, KJ_STR_OPP_EMPTY, LV_ALIGN_TOP_MID, 0, 160);
        kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_OPP_EMPTY2, LV_ALIGN_TOP_MID, 0, 188);
    } else {
        int first = m->opp_sel >= 3 ? m->opp_sel - 2 : 0;
        if (first > m->opp_count - 4) first = m->opp_count - 4 > 0 ? m->opp_count - 4 : 0;
        for (int i = first; i < m->opp_count && i < first + 4; i++) {
            const kj_opponent_t *o = &m->opps[i];
            bool sel = i == m->opp_sel;
            int y = 100 + (i - first) * 46;
            lv_obj_t *row = kj_box(s, 18, y, 204, 40, sel ? 0x3A1517 : KJ_C_PANEL, 10);
            lv_obj_set_style_border_width(row, sel ? 2 : 1, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(sel ? KJ_C_RED : KJ_C_LINE), 0);
            char buf[16];
            no_text(buf, sizeof(buf), o->no);
            kj_text_at(row, &kj_zh26, sel ? KJ_C_TEXT : 0xD9CDBA, buf, LV_ALIGN_LEFT_MID, 14, -1);
            if (o->is_bot) {
                // 电脑选手在庄家设备里，没有信号强弱可言
                lv_obj_t *tag = kj_box(row, 92, 10, 44, 20, 0x2E3A44, 10);
                lv_obj_center(kj_text(tag, &kj_zh14, KJ_C_BLUE, KJ_STR_BOT));
            } else {
                kj_text_at(row, &kj_zh14, o->nearby ? KJ_C_MUTED : KJ_C_DIM, kj_signal_word(o),
                           LV_ALIGN_RIGHT_MID, -36, 0);
                kj_signal(row, 178, 13, o->nearby ? kj_ui_signal_level(o->rssi) : 0,
                          sel ? KJ_C_GOLD : KJ_C_MUTED);
            }
        }
        char pos[12];
        snprintf(pos, sizeof(pos), "%u/%u", (unsigned)m->opp_sel + 1, (unsigned)m->opp_count);
        kj_text_at(s, &kj_zh14, KJ_C_DIM, pos, LV_ALIGN_TOP_RIGHT, -22, 80);
    }
    kj_footer(s, KJ_STR_HINT_OPP);
}

// ---------------------------------------------------------------------------
// 等待应战 / 收到挑战（共用倒计时条）
// ---------------------------------------------------------------------------
static void countdown(lv_obj_t *s, const char *fmt, int y, uint32_t color)
{
    kj_ui.countdown_fmt = fmt;
    kj_ui.countdown_label = kj_text_at(s, &kj_zh18, KJ_C_TEXT, "", LV_ALIGN_TOP_MID, 0, y);
    lv_obj_t *bar = lv_bar_create(s);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 180, 6);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, y + 30);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A2F28), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, KJ_CHALLENGE_TIMEOUT_MS / 1000);
    lv_bar_set_value(bar, KJ_CHALLENGE_TIMEOUT_MS / 1000, LV_ANIM_OFF);
    kj_ui.countdown_bar = bar;
}

static void page_wait(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    kj_text_at(s, &kj_zh26, KJ_C_TEXT, KJ_STR_WAIT_TITLE, LV_ALIGN_TOP_MID, 0, 44);
    kj_badge(s, 120, 146, 110, m->view.peer_no, KJ_C_GOLD, true);
    if (m->view.peer_is_bot) {
        lv_obj_t *tag = kj_box(s, 100, 196, 40, 20, 0x2E3A44, 10);
        lv_obj_center(kj_text(tag, &kj_zh14, KJ_C_BLUE, KJ_STR_BOT));
    }
    countdown(s, KJ_STR_WAIT_FMT, 224, KJ_C_GOLD);
    kj_footer(s, KJ_STR_HINT_WAIT);
}

static void page_challenged(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    player_top_bar(m);
    lv_obj_t *title = kj_text_at(s, &kj_big48, KJ_C_TEXT, KJ_BIG_CHALLENGE, LV_ALIGN_TOP_MID, 0, 36);
    kj_pulse(title, 900);
    kj_badge(s, 120, 146, 92, m->view.peer_no, KJ_C_RED, true);
    kj_text_at(s, &kj_zh18, KJ_C_TEXT, m->view.peer_is_bot ? KJ_STR_CHAL_BOT : KJ_STR_CHAL_FROM,
               LV_ALIGN_TOP_MID, 0, 198);
    countdown(s, KJ_STR_CHAL_LEFT_FMT, 222, KJ_C_RED);
    kj_pill(s, 22, 262, 100, 30, KJ_STR_ACCEPT, true, KJ_C_GOLD);
    kj_pill(s, 128, 262, 90, 30, KJ_STR_DECLINE, false, 0);
}

// ---------------------------------------------------------------------------
// 出牌
// ---------------------------------------------------------------------------
static void page_choose(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    char buf[48];
    snprintf(buf, sizeof(buf), KJ_STR_DUEL_VS_FMT, v->peer_no);
    kj_top_bar(s, buf, KJ_C_TEXT);
    // 对手状态
    lv_obj_t *chip = kj_box(s, 50, 40, 140, 26, v->peer_locked ? 0x3B2E12 : KJ_C_PANEL, 13);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(v->peer_locked ? KJ_C_GOLD : KJ_C_LINE), 0);
    lv_obj_t *ct = kj_text(chip, &kj_zh14, v->peer_locked ? KJ_C_GOLD : KJ_C_MUTED,
                           v->peer_locked ? KJ_STR_OPP_LOCKED : KJ_STR_OPP_THINKING);
    lv_obj_center(ct);
    if (!v->peer_locked) kj_pulse(ct, 1200);

    bool locked = v->my_lock != KJ_CARD_NONE;
    kj_text_at(s, &kj_zh18, locked ? KJ_C_GOLD : KJ_C_TEXT, locked ? KJ_STR_LOCKED_WAIT : KJ_STR_PICK_PROMPT,
               LV_ALIGN_TOP_MID, 0, 76);
    for (int c = 0; c < KJ_CARD_TYPES; c++) {
        uint32_t flags = KJ_CARD_COUNT;
        if (locked) {
            flags = c == v->my_lock ? (KJ_CARD_BACK | KJ_CARD_SEL) : (KJ_CARD_OFF | KJ_CARD_COUNT);
        } else {
            if (v->cards[c] == 0) flags |= KJ_CARD_OFF;
            if (c == m->card_sel) flags |= KJ_CARD_SEL;
        }
        kj_card(s, 14 + c * 74, 116, 66, 100, (uint8_t)c, v->cards[c], flags);
    }
    uint8_t shown = locked ? v->my_lock : m->card_sel;
    if (shown < KJ_CARD_TYPES) {
        snprintf(buf, sizeof(buf), KJ_STR_CARD_LEFT_FMT, kj_card_name(shown), (unsigned)v->cards[shown]);
        kj_text_at(s, &kj_zh18, locked ? KJ_C_MUTED : KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 232);
    }
    kj_footer(s, locked ? KJ_STR_HINT_LOCKED : KJ_STR_HINT_CHOOSE);
}

// ---------------------------------------------------------------------------
// 亮牌
// ---------------------------------------------------------------------------
static void page_reveal(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    player_top_bar(m);
    char buf[16];
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_ME, LV_ALIGN_TOP_LEFT, 56, 42);
    no_text(buf, sizeof(buf), v->res_opp_no);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, buf, LV_ALIGN_TOP_RIGHT, -46, 42);
    uint8_t out = v->res_outcome;
    kj_card(s, 18, 64, 84, 112, v->res_my, 0, KJ_CARD_BIG | KJ_CARD_NAME |
            (out == KJ_OUT_LOSE ? KJ_CARD_OFF : 0));
    kj_card(s, 138, 64, 84, 112, v->res_opp, 0, KJ_CARD_BIG | KJ_CARD_NAME |
            (out == KJ_OUT_WIN ? KJ_CARD_OFF : 0));
    kj_text_at(s, &kj_zh26, KJ_C_DIM, KJ_BIG_VS, LV_ALIGN_TOP_MID, 0, 104);

    uint32_t color = out == KJ_OUT_WIN ? KJ_C_GOLD : out == KJ_OUT_LOSE ? KJ_C_RED : KJ_C_BLUE;
    const char *big = out == KJ_OUT_WIN ? KJ_BIG_WIN : out == KJ_OUT_LOSE ? KJ_BIG_LOSE : KJ_BIG_DRAW;
    const char *delta = out == KJ_OUT_WIN ? KJ_STR_STAR_PLUS : out == KJ_OUT_LOSE ? KJ_STR_STAR_MINUS
                                                                                 : KJ_STR_STAR_SAME;
    lv_obj_t *band = kj_box(s, 0, 186, 240, 62, out == KJ_OUT_LOSE ? 0x2A0C0E : 0x221C12, 0);
    lv_obj_set_style_border_side(band, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(band, 2, 0);
    lv_obj_set_style_border_color(band, lv_color_hex(color), 0);
    kj_text_at(band, &kj_big48, color, big, LV_ALIGN_LEFT_MID, 52, -2);
    kj_text_at(band, &kj_zh26, color, delta, LV_ALIGN_LEFT_MID, 118, 0);
    kj_stars(s, 254, v->stars, &kj_zh18);
    kj_footer(s, KJ_STR_HINT_REVEAL);
}

// ---------------------------------------------------------------------------
// 终局
// ---------------------------------------------------------------------------
static void page_final(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    const kj_view_t *v = &m->view;
    player_top_bar(m);
    uint32_t color;
    const char *big;
    char sub[64];
    if (v->status == KJ_ST_CLEARED) {
        color = KJ_C_GOLD;
        big = KJ_BIG_CLEARED;
        snprintf(sub, sizeof(sub), KJ_STR_CLEARED_FMT, (unsigned)v->stars);
    } else if (v->status == KJ_ST_ELIMINATED) {
        color = KJ_C_RED;
        big = KJ_BIG_OUT;
        snprintf(sub, sizeof(sub), "%s", KJ_STR_OUT_SUB);
    } else {
        color = 0xB0453F;
        big = KJ_BIG_FAILED;
        snprintf(sub, sizeof(sub), "%s",
                 v->final_reason == KJ_FINAL_TIME_UP ? KJ_STR_FAIL_TIMEUP : KJ_STR_FAIL_NOCARD);
    }
    // 印章：双圈 + 大字
    kj_frame(s, 50, 50, 140, 140, color, 4, LV_RADIUS_CIRCLE);
    kj_frame(s, 60, 60, 120, 120, color, 1, LV_RADIUS_CIRCLE);
    kj_text_at(s, &kj_big48, color, big, LV_ALIGN_TOP_MID, 0, 88);
    kj_text_at(s, &kj_zh18, KJ_C_TEXT, sub, LV_ALIGN_TOP_MID, 0, 204);
    kj_stars(s, 230, v->stars, &kj_zh26);
    char rec[48];
    snprintf(rec, sizeof(rec), KJ_STR_RECORD_FMT, v->wins, v->losses, v->draws);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, rec, LV_ALIGN_TOP_MID, 0, 266);
    kj_footer(s, KJ_STR_FINAL_WAIT);
}

// ---------------------------------------------------------------------------
// 庄家面板（只显示汇总，不显示任何人的手牌）
// ---------------------------------------------------------------------------
static const char *host_item_text(int item)
{
    static const char *const names[KJ_HM_COUNT] = {
        KJ_STR_M_START, KJ_STR_M_END, KJ_STR_M_NEW, KJ_STR_M_BOT_ADD, KJ_STR_M_BOT_DEL, KJ_STR_M_RESET,
    };
    return item >= 0 && item < KJ_HM_COUNT ? names[item] : "";
}

static void stat_cell(lv_obj_t *s, int x, int value, const char *label)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    lv_obj_t *cell = kj_box(s, x, 144, 46, 52, KJ_C_PANEL, 8);
    kj_text_at(cell, &kj_zh26, KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 2);
    kj_text_at(cell, &kj_zh14, KJ_C_MUTED, label, LV_ALIGN_BOTTOM_MID, 0, -3);
}

static void page_host(const kj_ui_model_t *m)
{
    lv_obj_t *s = kj_ui.scr;
    kj_top_bar(s, KJ_STR_HOST_TAG, KJ_C_GOLD);
    kj_text_at(s, &kj_zh14, KJ_C_MUTED, KJ_STR_HOST_ROOM, LV_ALIGN_TOP_MID, 0, 30);
    char room[5];
    snprintf(room, sizeof(room), "%04X", (unsigned)m->host_room);
    kj_text_at(s, &kj_num56, KJ_C_TEXT, room, LV_ALIGN_TOP_MID, 0, 50);
    uint32_t pc = m->host_phase == KJ_PHASE_RUNNING ? KJ_C_RED : m->host_phase == KJ_PHASE_ENDED ? KJ_C_BLUE
                                                                                             : KJ_C_GOLD;
    lv_obj_t *chip = kj_box(s, 44, 112, 152, 24, 0x1E1714, 12);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(pc), 0);
    kj_text_at(chip, &kj_zh14, pc, phase_text(m->host_phase), LV_ALIGN_LEFT_MID, 14, 0);
    kj_ui.clock_label = kj_text_at(chip, &kj_zh14, KJ_C_MUTED, "", LV_ALIGN_RIGHT_MID, -14, 0);
    const kj_summary_t *sum = &m->host_sum;
    stat_cell(s, 20, sum->seated, KJ_STR_STAT_SEATED);
    stat_cell(s, 72, sum->online, KJ_STR_STAT_ONLINE);
    stat_cell(s, 124, sum->in_duel, KJ_STR_STAT_DUELS);
    stat_cell(s, 176, sum->cleared + sum->eliminated + sum->failed, KJ_STR_STAT_DONE);

    // 菜单：显示选中项及其前后各一项
    for (int k = -1; k <= 1; k++) {
        int item = (m->host_sel + k + KJ_HM_COUNT) % KJ_HM_COUNT;
        bool sel = k == 0;
        bool on = (m->host_enabled >> item) & 1u;
        int y = 210 + (k + 1) * 26;
        if (sel) {
            lv_obj_t *row = kj_box(s, 20, y - 2, 200, 26, on ? KJ_C_RED : 0x3A2F28, 8);
            (void)row;
        }
        kj_text_at(s, sel ? &kj_zh18 : &kj_zh14, sel ? (on ? KJ_C_TEXT : KJ_C_MUTED) : (on ? 0xBDB09C : KJ_C_DIM),
                   host_item_text(item), LV_ALIGN_TOP_MID, 0, y + (sel ? 0 : 3));
    }
    kj_footer(s, m->usb ? KJ_STR_USB_ON : KJ_STR_USB_OFF);

    if (m->host_confirm >= 0) {
        lv_obj_t *dlg = kj_box(s, 22, 112, 196, 104, 0x241C18, 14);
        lv_obj_set_style_border_width(dlg, 2, 0);
        lv_obj_set_style_border_color(dlg, lv_color_hex(KJ_C_RED), 0);
        char buf[48];
        snprintf(buf, sizeof(buf), KJ_STR_CONFIRM_FMT, host_item_text(m->host_confirm));
        kj_text_at(dlg, &kj_zh18, KJ_C_TEXT, buf, LV_ALIGN_TOP_MID, 0, 22);
        kj_text_at(dlg, &kj_zh14, KJ_C_MUTED, KJ_STR_CONFIRM_HINT, LV_ALIGN_TOP_MID, 0, 62);
    }
}

void kj_ui_build_page(const kj_ui_model_t *m)
{
    switch (m->page) {
    case KJ_PAGE_TITLE: page_title(m); break;
    case KJ_PAGE_ROOMS: page_rooms(m); break;
    case KJ_PAGE_SEAT: page_seat(m); break;
    case KJ_PAGE_HAND: page_hand(m); break;
    case KJ_PAGE_OPPONENTS: page_opponents(m); break;
    case KJ_PAGE_WAIT: page_wait(m); break;
    case KJ_PAGE_CHALLENGED: page_challenged(m); break;
    case KJ_PAGE_CHOOSE: page_choose(m); break;
    case KJ_PAGE_REVEAL: page_reveal(m); break;
    case KJ_PAGE_FINAL: page_final(m); break;
    case KJ_PAGE_HOST: page_host(m); break;
    default: page_title(m); break;
    }
}
