// main/kj_server.c —— 庄家侧协议逻辑（纯 C）。
#include "kj_server.h"

#include <string.h>

static uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

uint32_t kj_bot_accept_delay(uint16_t salt, int idx)
{
    return 1200u + mix(((uint32_t)salt << 8) ^ (uint32_t)idx ^ 0xA5A5u) % 1800u;
}

uint32_t kj_bot_play_delay(uint16_t duel_id, int idx)
{
    return 900u + mix(((uint32_t)duel_id << 8) ^ (uint32_t)idx ^ 0x5A5Au) % 2600u;
}

uint32_t kj_bot_idle_delay(uint32_t salt, int idx)
{
    return 4000u + mix(salt ^ ((uint32_t)idx << 16) ^ 0x3C3Cu) % 6000u;
}

uint8_t kj_bot_choose(const kj_player_t *p, uint32_t *rng)
{
    int total = p->cards[0] + p->cards[1] + p->cards[2];
    if (total <= 0) return KJ_CARD_NONE;
    uint32_t r = xorshift32(rng);
    if (r % 100u < 60u) {
        // 六成概率出剩余最多的牌（多张并列时随机挑一张）。
        int best = 0;
        for (int c = 0; c < KJ_CARD_TYPES; c++) best = p->cards[c] > best ? p->cards[c] : best;
        uint8_t pick[KJ_CARD_TYPES];
        int n = 0;
        for (int c = 0; c < KJ_CARD_TYPES; c++) {
            if (p->cards[c] == best) pick[n++] = (uint8_t)c;
        }
        return pick[xorshift32(rng) % (uint32_t)n];
    }
    // 其余按剩余张数加权随机。
    int x = (int)(xorshift32(rng) % (uint32_t)total);
    for (int c = 0; c < KJ_CARD_TYPES; c++) {
        if (x < p->cards[c]) return (uint8_t)c;
        x -= p->cards[c];
    }
    return KJ_CARD_NONE;
}

void kj_server_init(kj_server_t *s, uint16_t room, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    kj_rules_init(&s->game);
    s->room = room;
    s->rng = seed ? seed : 0x2545F491u;
    kj_rules_mark_all_dirty(&s->game);
}

void kj_server_room_info(const kj_server_t *s, uint32_t now_ms, kj_room_t *out)
{
    memset(out, 0, sizeof(*out));
    const kj_game_t *g = &s->game;
    out->phase = g->phase;
    out->game_id = g->game_id;
    out->phase_ver = g->phase_ver;
    int seated = 0, online = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        const kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        seated++;
        if (kj_rules_online(g, i, now_ms)) online++;
        if (kj_rules_available(g, i, now_ms)) kj_bit_set(out->avail, kj_no_of(i));
        if (p->is_bot) kj_bit_set(out->bots, kj_no_of(i));
    }
    out->seated = (uint8_t)seated;
    out->online = (uint8_t)online;
}

static void send_view(kj_server_t *s, int idx, const uint8_t mac[6], uint32_t now_ms, kj_outbox_t *out)
{
    kj_frame_t f = { .type = KJ_F_VIEW, .room = s->room };
    kj_rules_view(&s->game, idx, now_ms, &f.u.view);
    if (kj_outbox_push(out, mac, &f)) s->tx_views++;
}

// 给不在册的设备回一个 no=0 的视图，让它回到找赌局页面（可附带原因）。
static void send_not_member(kj_server_t *s, const uint8_t mac[6], kj_notice_t why, uint32_t now_ms,
                            kj_outbox_t *out)
{
    kj_frame_t f = { .type = KJ_F_VIEW, .room = s->room };
    kj_rules_view(&s->game, -1, now_ms, &f.u.view);
    f.u.view.notice = (uint8_t)why;
    f.u.view.notice_seq = 1;
    kj_outbox_push(out, mac, &f);
}

static void push_views(kj_server_t *s, uint32_t now_ms, kj_outbox_t *out)
{
    kj_game_t *g = &s->game;
    int sent = 0;
    int start = s->view_cursor;
    for (int k = 0; k < KJ_MAX_PLAYERS && sent < KJ_VIEW_SENDS_PER_TICK; k++) {
        int i = (start + k) % KJ_MAX_PLAYERS;
        kj_player_t *p = &g->players[i];
        if (!p->used || p->is_bot || !kj_rules_online(g, i, now_ms)) continue;
        kj_link_t *l = &s->link[i];
        if (p->view_ver == l->acked_ver && !l->force) continue;
        bool fresh = p->view_ver != l->sent_ver;
        bool retry = l->tries < KJ_VIEW_RETRY_MAX && (uint32_t)(now_ms - l->sent_ms) >= KJ_VIEW_RETRY_MS;
        if (!fresh && !l->force && !retry) continue;
        if (fresh) l->tries = 0;
        send_view(s, i, p->mac, now_ms, out);
        l->sent_ver = p->view_ver;
        l->sent_ms = now_ms;
        l->tries++;
        l->force = 0;
        sent++;
        s->view_cursor = (uint8_t)((i + 1) % KJ_MAX_PLAYERS);
    }
}

static void apply_request(kj_server_t *s, int idx, const kj_req_t *req, uint32_t now_ms)
{
    kj_game_t *g = &s->game;
    kj_notice_t n = KJ_N_INVALID;
    switch (req->op) {
    case KJ_OP_CHALLENGE: n = kj_rules_challenge(g, idx, req->arg, now_ms); break;
    case KJ_OP_CANCEL: n = kj_rules_cancel(g, idx, now_ms); break;
    case KJ_OP_ACCEPT: n = kj_rules_respond(g, idx, true, now_ms); break;
    case KJ_OP_DECLINE: n = kj_rules_respond(g, idx, false, now_ms); break;
    case KJ_OP_PLAY: n = kj_rules_play(g, idx, req->arg, now_ms); break;
    case KJ_OP_WITHDRAW: n = kj_rules_withdraw(g, idx, now_ms); break;
    default: break;
    }
    if (n != KJ_N_NONE) kj_rules_notify(g, idx, n);
}

void kj_server_on_frame(kj_server_t *s, const uint8_t mac[6], int8_t rssi,
                        const uint8_t *data, size_t len, uint32_t now_ms, kj_outbox_t *out)
{
    kj_frame_t f;
    if (!kj_proto_decode(data, len, &f)) {
        s->rx_bad++;
        return;
    }
    s->rx_frames++;
    if (f.room != s->room) return;   // 别的赌局、或尚未入座的心跳
    kj_game_t *g = &s->game;
    int idx = kj_rules_find_mac(g, mac);

    if (f.type == KJ_F_HELLO) {
        if (idx < 0) {
            if (f.u.hello.flags & KJ_HELLO_JOINED) send_not_member(s, mac, KJ_N_NONE, now_ms, out);
            return;
        }
        kj_rules_seen(g, idx, rssi, now_ms);
        kj_link_t *l = &s->link[idx];
        l->acked_ver = f.u.hello.view_ver;
        if (l->acked_ver != g->players[idx].view_ver) l->force = 1;
        push_views(s, now_ms, out);
        return;
    }
    if (f.type != KJ_F_REQ) return;

    const kj_req_t *req = &f.u.req;
    if (req->op == KJ_OP_JOIN) {
        if (idx >= 0 && req->seq == g->players[idx].last_req_seq) {
            kj_rules_seen(g, idx, rssi, now_ms);   // 重复的入座请求：只补发视图
            s->link[idx].force = 1;
        } else {
            idx = kj_rules_join(g, mac, now_ms);
            if (idx < 0) {
                send_not_member(s, mac, KJ_N_FULL, now_ms, out);
                return;
            }
            // 设备重启后序号从头开始：入座请求总是重置去重基准。
            g->players[idx].last_req_seq = req->seq;
            kj_rules_seen(g, idx, rssi, now_ms);
            kj_rules_touch(g, idx);
            s->link[idx].force = 1;
            s->link[idx].tries = 0;
        }
        push_views(s, now_ms, out);
        return;
    }
    if (idx < 0) {
        send_not_member(s, mac, KJ_N_NONE, now_ms, out);
        return;
    }
    kj_rules_seen(g, idx, rssi, now_ms);
    kj_player_t *p = &g->players[idx];
    if (kj_seq_diff(req->seq, p->last_req_seq) <= 0) {
        s->link[idx].force = 1;   // 重复 / 过期请求：不再执行，只补发视图（确认已送达）
    } else {
        p->last_req_seq = req->seq;
        apply_request(s, idx, req, now_ms);
        kj_rules_touch(g, idx);   // 即使失败也要把 ack_seq 送回去
    }
    push_views(s, now_ms, out);
}

static void bots_step(kj_server_t *s, uint32_t now_ms)
{
    kj_game_t *g = &s->game;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        kj_player_t *p = &g->players[i];
        if (!p->used || !p->is_bot) continue;
        uint32_t el = now_ms - p->since_ms;
        if (p->status == KJ_ST_CHALLENGED && el >= kj_bot_accept_delay((uint16_t)p->since_ms, i)) {
            kj_rules_respond(g, i, true, now_ms);
        } else if (p->status == KJ_ST_DUEL && p->locked == KJ_CARD_NONE &&
                   el >= kj_bot_play_delay(p->duel_id, i)) {
            uint8_t card = kj_bot_choose(p, &s->rng);
            if (card != KJ_CARD_NONE) kj_rules_play(g, i, card, now_ms);
        } else if (p->status == KJ_ST_IDLE && el >= kj_bot_idle_delay(p->since_ms, i)) {
            // 电脑选手只会主动找别的电脑选手对决（不打扰真人），保证只剩电脑时也能打完。
            int start = (int)(xorshift32(&s->rng) % KJ_MAX_PLAYERS);
            for (int k = 0; k < KJ_MAX_PLAYERS; k++) {
                int t = (start + k) % KJ_MAX_PLAYERS;
                if (t != i && g->players[t].is_bot && kj_rules_available(g, t, now_ms)) {
                    kj_rules_challenge(g, i, kj_no_of(t), now_ms);
                    break;
                }
            }
            if (p->status == KJ_ST_IDLE) p->since_ms = now_ms;   // 没找到：过一会儿再试
        }
    }
}

void kj_server_tick(kj_server_t *s, uint32_t now_ms, kj_outbox_t *out)
{
    kj_game_t *g = &s->game;
    kj_rules_tick(g, now_ms);
    bots_step(s, now_ms);

    uint32_t since = now_ms - s->last_beacon_ms;
    bool changed = g->phase_ver != s->beacon_phase_ver;
    if (!s->beacon_sent || since >= KJ_ROOM_BEACON_MS || (changed && since >= KJ_ROOM_BEACON_MIN_MS)) {
        kj_frame_t f = { .type = KJ_F_ROOM, .room = s->room };
        kj_server_room_info(s, now_ms, &f.u.room_info);
        if (kj_outbox_push(out, NULL, &f)) {
            s->tx_beacons++;
            s->last_beacon_ms = now_ms;
            s->beacon_phase_ver = g->phase_ver;
            s->beacon_sent = true;
        }
    }
    push_views(s, now_ms, out);
}

kj_notice_t kj_server_command(kj_server_t *s, kj_cmd_t cmd, int arg, uint32_t now_ms)
{
    kj_game_t *g = &s->game;
    switch (cmd) {
    case KJ_CMD_START: return kj_rules_start(g, now_ms);
    case KJ_CMD_END: return kj_rules_end(g, now_ms);
    case KJ_CMD_NEW_GAME: kj_rules_new_game(g, now_ms); return KJ_N_NONE;
    case KJ_CMD_RESET: kj_rules_reset(g, now_ms); return KJ_N_NONE;
    case KJ_CMD_BOT_ADD: return kj_rules_add_bot(g, now_ms) >= 0 ? KJ_N_NONE : KJ_N_FULL;
    case KJ_CMD_BOT_REMOVE: return kj_rules_remove_bot(g, now_ms);
    case KJ_CMD_KICK:
        if (arg < 1 || arg > KJ_MAX_PLAYERS) return KJ_N_INVALID;
        return kj_rules_remove(g, kj_idx_of((uint8_t)arg), now_ms);
    case KJ_CMD_SYNC: kj_rules_mark_all_dirty(g); return KJ_N_NONE;
    default: return KJ_N_INVALID;
    }
}
