// main/kj_flow.c —— 选手 / 庄家界面状态机（纯 C）。
#include "kj_flow.h"

#include <string.h>

void kj_flow_init(kj_flow_t *f, uint8_t title_sel)
{
    memset(f, 0, sizeof(*f));
    f->title_sel = title_sel ? 1 : 0;
    f->host_confirm = -1;
    f->last_page = KJ_PAGE_TITLE;
}

void kj_flow_toast(kj_flow_t *f, kj_toast_t t, uint32_t now_ms)
{
    f->toast = (uint8_t)t;
    f->toast_until = now_ms + KJ_TOAST_MS;
}

kj_toast_t kj_flow_active_toast(const kj_flow_t *f, uint32_t now_ms)
{
    if (f->toast == KJ_TOAST_NONE || (int32_t)(now_ms - f->toast_until) >= 0) return KJ_TOAST_NONE;
    return (kj_toast_t)f->toast;
}

kj_toast_t kj_flow_toast_for_notice(uint8_t notice)
{
    switch (notice) {
    case KJ_N_DECLINED: return KJ_TOAST_DECLINED;
    case KJ_N_CANCELLED: return KJ_TOAST_CANCELLED;
    case KJ_N_TIMEOUT: return KJ_TOAST_TIMEOUT;
    case KJ_N_WITHDRAWN: return KJ_TOAST_WITHDRAWN;
    case KJ_N_ABORTED: return KJ_TOAST_ABORTED;
    case KJ_N_BUSY: return KJ_TOAST_BUSY;
    case KJ_N_NOT_RUNNING: return KJ_TOAST_NOT_RUNNING;
    case KJ_N_NO_CARD: return KJ_TOAST_NO_CARD;
    case KJ_N_INVALID: return KJ_TOAST_INVALID;
    case KJ_N_FULL: return KJ_TOAST_FULL;
    default: return KJ_TOAST_NONE;
    }
}

kj_action_t kj_flow_title_key(kj_flow_t *f, kj_key_t key)
{
    kj_action_t a = { 0 };
    if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
        f->title_sel ^= 1;
    } else if (key == KJ_KEY_OK) {
        a.kind = KJ_ACT_ROLE;
        a.arg = f->title_sel;
    }
    return a;
}

uint8_t kj_flow_valid_card(const kj_view_t *v, uint8_t preferred, int direction)
{
    int dir = direction < 0 ? -1 : 1;
    int c = preferred % KJ_CARD_TYPES;
    for (int k = 0; k < KJ_CARD_TYPES; k++) {
        if (v->cards[c] > 0) return (uint8_t)c;
        c = (c + dir + KJ_CARD_TYPES) % KJ_CARD_TYPES;
    }
    return KJ_CARD_NONE;
}

static kj_page_t derive_page(const kj_flow_t *f, const kj_view_t *v)
{
    if (f->reveal_active) return KJ_PAGE_REVEAL;
    if (v->phase == KJ_PHASE_LOBBY || v->status == KJ_ST_WAITING) return KJ_PAGE_SEAT;
    switch (v->status) {
    case KJ_ST_IDLE: return f->picking ? KJ_PAGE_OPPONENTS : KJ_PAGE_HAND;
    case KJ_ST_CHALLENGING: return KJ_PAGE_WAIT;
    case KJ_ST_CHALLENGED: return KJ_PAGE_CHALLENGED;
    case KJ_ST_DUEL: return KJ_PAGE_CHOOSE;
    default: return KJ_PAGE_FINAL;
    }
}

static void sync_opp_selection(kj_flow_t *f, const kj_player_ctx_t *ctx)
{
    if (ctx->opp_count <= 0) {
        f->opp_sel = 0;
        f->opp_no = 0;
        return;
    }
    for (int i = 0; i < ctx->opp_count; i++) {
        if (ctx->opps[i].no == f->opp_no) {
            f->opp_sel = (uint8_t)i;
            return;
        }
    }
    if (f->opp_sel >= ctx->opp_count) f->opp_sel = (uint8_t)(ctx->opp_count - 1);
    f->opp_no = ctx->opps[f->opp_sel].no;
}

kj_page_t kj_flow_player_update(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_cue_t *cue)
{
    kj_cue_t out = KJ_CUE_NONE;
    const kj_client_t *c = ctx->client;
    kj_page_t page;
    if (c->link != KJ_LINK_JOINED) {
        f->picking = false;
        f->reveal_active = false;
        f->primed = false;
        if (ctx->room_count <= 0) {
            f->room_sel = 0;
        } else if (f->room_sel >= ctx->room_count) {
            f->room_sel = (uint8_t)(ctx->room_count - 1);
        }
        page = KJ_PAGE_ROOMS;
    } else {
        const kj_view_t *v = &c->view;
        if (!f->primed || f->room != c->room) {
            // 第一次拿到视图（入座 / 重连）：历史通知与已经结算过的亮牌不再重播。
            f->primed = true;
            f->room = c->room;
            f->game_id = v->game_id;
            f->notice_seq = v->notice_seq;
            f->shown_res_duel = v->res_duel_id;
            f->last_status = v->status;
            f->picking = false;
            f->reveal_active = false;
        }
        if (v->game_id != f->game_id) {
            f->game_id = v->game_id;
            f->picking = false;
            f->reveal_active = false;
            f->shown_res_duel = v->res_duel_id;
        }
        if (v->notice_seq != f->notice_seq) {
            f->notice_seq = v->notice_seq;
            kj_toast_t t = kj_flow_toast_for_notice(v->notice);
            if (t != KJ_TOAST_NONE) {
                kj_flow_toast(f, t, ctx->now_ms);
                out = KJ_CUE_NOTICE;
            }
        }
        if (!f->reveal_active && v->res_duel_id != 0 && v->res_duel_id != f->shown_res_duel &&
            v->res_outcome != KJ_OUT_NONE) {
            f->reveal_active = true;
            f->reveal_since = ctx->now_ms;
            out = v->res_outcome == KJ_OUT_WIN ? KJ_CUE_WIN
                : v->res_outcome == KJ_OUT_LOSE ? KJ_CUE_LOSE : KJ_CUE_DRAW;
        }
        if (f->reveal_active && (uint32_t)(ctx->now_ms - f->reveal_since) >= KJ_REVEAL_AUTO_MS) {
            f->reveal_active = false;
            f->shown_res_duel = v->res_duel_id;
        }
        if (v->status != KJ_ST_IDLE) f->picking = false;
        if (v->status == KJ_ST_DUEL && v->status != f->last_status) {
            uint8_t card = kj_flow_valid_card(v, KJ_SCISSORS, 1);   // 进入对决默认停在中间那张
            f->card_sel = card == KJ_CARD_NONE ? 0 : card;
        }
        f->last_status = v->status;
        page = derive_page(f, v);
        if (page == KJ_PAGE_OPPONENTS) sync_opp_selection(f, ctx);
        if (page == KJ_PAGE_CHOOSE && v->my_lock == KJ_CARD_NONE && v->cards[f->card_sel % 3] == 0) {
            uint8_t card = kj_flow_valid_card(v, f->card_sel, 1);
            f->card_sel = card == KJ_CARD_NONE ? 0 : card;
        }
        if (page != f->last_page && out == KJ_CUE_NONE) {
            if (page == KJ_PAGE_CHALLENGED) out = KJ_CUE_ALERT;
            else if (page == KJ_PAGE_CHOOSE) out = KJ_CUE_DUEL;
            else if (page == KJ_PAGE_FINAL) out = v->status == KJ_ST_CLEARED ? KJ_CUE_CLEARED : KJ_CUE_OUT;
        }
    }
    f->last_page = (uint8_t)page;
    if (cue) *cue = out;
    return page;
}

static uint8_t wrap(int v, int n)
{
    if (n <= 0) return 0;
    return (uint8_t)(((v % n) + n) % n);
}

kj_action_t kj_flow_player_key(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_key_t key)
{
    kj_action_t a = { 0 };
    const kj_view_t *v = &ctx->client->view;
    switch ((kj_page_t)f->last_page) {
    case KJ_PAGE_ROOMS:
        if (key == KJ_KEY_UP) f->room_sel = wrap(f->room_sel - 1, ctx->room_count);
        if (key == KJ_KEY_DOWN) f->room_sel = wrap(f->room_sel + 1, ctx->room_count);
        if (key == KJ_KEY_OK && ctx->room_count > 0 && ctx->client->link == KJ_LINK_IDLE) {
            a.kind = KJ_ACT_JOIN;
            a.room = ctx->rooms[f->room_sel % ctx->room_count].room;
        }
        if (key == KJ_KEY_OK_LONG) a.kind = KJ_ACT_TO_TITLE;
        break;
    case KJ_PAGE_SEAT:
        if (key == KJ_KEY_OK_LONG) a.kind = KJ_ACT_LEAVE;
        break;
    case KJ_PAGE_HAND:
        if (key == KJ_KEY_OK) {
            f->picking = true;
            f->opp_sel = 0;
            f->opp_no = ctx->opp_count > 0 ? ctx->opps[0].no : 0;
            f->last_page = KJ_PAGE_OPPONENTS;
        }
        break;
    case KJ_PAGE_OPPONENTS:
        if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
            f->opp_sel = wrap(f->opp_sel + (key == KJ_KEY_UP ? -1 : 1), ctx->opp_count);
            f->opp_no = ctx->opp_count > 0 ? ctx->opps[f->opp_sel].no : 0;
        } else if (key == KJ_KEY_OK && ctx->opp_count > 0) {
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_CHALLENGE;
            a.arg = ctx->opps[f->opp_sel % ctx->opp_count].no;
        } else if (key == KJ_KEY_OK_LONG) {
            f->picking = false;
            f->last_page = KJ_PAGE_HAND;
        }
        break;
    case KJ_PAGE_WAIT:
        if (key == KJ_KEY_OK_LONG) {
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_CANCEL;
        }
        break;
    case KJ_PAGE_CHALLENGED:
        if (key == KJ_KEY_OK || key == KJ_KEY_OK_LONG) {
            a.kind = KJ_ACT_REQUEST;
            a.op = key == KJ_KEY_OK ? KJ_OP_ACCEPT : KJ_OP_DECLINE;
        }
        break;
    case KJ_PAGE_CHOOSE:
        if (v->my_lock != KJ_CARD_NONE) break;   // 暗牌已扣下
        if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
            int dir = key == KJ_KEY_UP ? -1 : 1;
            uint8_t card = kj_flow_valid_card(v, wrap(f->card_sel + dir, KJ_CARD_TYPES), dir);
            if (card != KJ_CARD_NONE) f->card_sel = card;
        } else if (key == KJ_KEY_OK) {
            if (f->card_sel < KJ_CARD_TYPES && v->cards[f->card_sel] > 0) {
                a.kind = KJ_ACT_REQUEST;
                a.op = KJ_OP_PLAY;
                a.arg = f->card_sel;
            }
        } else if (key == KJ_KEY_OK_LONG) {
            a.kind = KJ_ACT_REQUEST;
            a.op = KJ_OP_WITHDRAW;
        }
        break;
    case KJ_PAGE_REVEAL:
        if (key == KJ_KEY_OK) {
            f->reveal_active = false;
            f->shown_res_duel = v->res_duel_id;
        }
        break;
    default:
        break;
    }
    return a;
}

bool kj_flow_host_item_enabled(kj_host_item_t item, const kj_game_t *g)
{
    int count = kj_rules_player_count(g);
    switch (item) {
    case KJ_HM_START: return g->phase == KJ_PHASE_LOBBY && count >= 2;
    case KJ_HM_END: return g->phase == KJ_PHASE_RUNNING;
    case KJ_HM_NEW: return g->phase != KJ_PHASE_LOBBY;
    case KJ_HM_BOT_ADD: return count < KJ_MAX_PLAYERS;
    case KJ_HM_BOT_DEL:
        for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
            if (g->players[i].used && g->players[i].is_bot) return true;
        }
        return false;
    case KJ_HM_RESET: return count > 0;
    default: return false;
    }
}

static kj_cmd_t host_cmd(kj_host_item_t item)
{
    switch (item) {
    case KJ_HM_START: return KJ_CMD_START;
    case KJ_HM_END: return KJ_CMD_END;
    case KJ_HM_NEW: return KJ_CMD_NEW_GAME;
    case KJ_HM_BOT_ADD: return KJ_CMD_BOT_ADD;
    case KJ_HM_BOT_DEL: return KJ_CMD_BOT_REMOVE;
    case KJ_HM_RESET: return KJ_CMD_RESET;
    default: return KJ_CMD_NONE;
    }
}

void kj_flow_host_sync(kj_flow_t *f, const kj_game_t *g)
{
    if (f->host_confirm >= 0 && !kj_flow_host_item_enabled((kj_host_item_t)f->host_confirm, g)) {
        f->host_confirm = -1;
    }
    if (kj_flow_host_item_enabled((kj_host_item_t)f->host_sel, g)) return;
    // 当前项不可用（例如刚开局后的"开始赌局"）：顺延到下一个可用项。
    for (int k = 1; k < KJ_HM_COUNT; k++) {
        int item = (f->host_sel + k) % KJ_HM_COUNT;
        if (kj_flow_host_item_enabled((kj_host_item_t)item, g)) {
            f->host_sel = (uint8_t)item;
            return;
        }
    }
}

kj_action_t kj_flow_host_key(kj_flow_t *f, const kj_game_t *g, kj_key_t key, uint32_t now_ms)
{
    kj_action_t a = { 0 };
    if (f->host_confirm >= 0) {
        if (key == KJ_KEY_OK && kj_flow_host_item_enabled((kj_host_item_t)f->host_confirm, g)) {
            a.kind = KJ_ACT_HOST_CMD;
            a.cmd = host_cmd((kj_host_item_t)f->host_confirm);
        }
        f->host_confirm = -1;   // 其他任何键都取消
        return a;
    }
    if (key == KJ_KEY_UP || key == KJ_KEY_DOWN) {
        // 跳过当前不可用的菜单项；全部不可用时按顺序移动。
        int dir = key == KJ_KEY_UP ? -1 : 1;
        int sel = f->host_sel;
        for (int k = 1; k <= KJ_HM_COUNT; k++) {
            int item = wrap(f->host_sel + dir * k, KJ_HM_COUNT);
            if (kj_flow_host_item_enabled((kj_host_item_t)item, g)) {
                sel = item;
                break;
            }
            if (k == KJ_HM_COUNT) sel = wrap(f->host_sel + dir, KJ_HM_COUNT);
        }
        f->host_sel = (uint8_t)sel;
    }
    if (key == KJ_KEY_OK) {
        kj_host_item_t item = (kj_host_item_t)f->host_sel;
        if (!kj_flow_host_item_enabled(item, g)) {
            bool need_two = item == KJ_HM_START && g->phase == KJ_PHASE_LOBBY;
            kj_flow_toast(f, need_two ? KJ_TOAST_NEED_TWO : KJ_TOAST_INVALID, now_ms);
            a.kind = KJ_ACT_NONE;
            a.arg = 1;            // 1 = 被拒绝（调用方据此播放错误音）
        } else if (item == KJ_HM_END || item == KJ_HM_NEW || item == KJ_HM_RESET) {
            f->host_confirm = (int8_t)item;
        } else {
            a.kind = KJ_ACT_HOST_CMD;
            a.cmd = host_cmd(item);
        }
    }
    return a;
}
