// main/kj_client.c —— 选手设备侧协议逻辑（纯 C）。
#include "kj_client.h"

#include <string.h>

static uint32_t rnd(kj_client_t *c)
{
    uint32_t x = c->rng ? c->rng : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    c->rng = x;
    return x;
}

static bool alive(uint32_t now_ms, uint32_t seen_ms, uint32_t ttl)
{
    return seen_ms != 0 && (uint32_t)(now_ms - seen_ms) < ttl;
}

void kj_client_init(kj_client_t *c, uint32_t seed)
{
    memset(c, 0, sizeof(*c));
    c->rng = seed ? seed : 0xC0FFEEu;
    c->seq = (uint16_t)(rnd(c) & 0x3FFF) + 1;
    c->boot = (uint16_t)(rnd(c) | 1u);   // 非 0
    c->view.my_lock = KJ_CARD_NONE;
    c->view.res_my = c->view.res_opp = KJ_CARD_NONE;
}


static void upsert_room(kj_client_t *c, const uint8_t mac[6], const kj_frame_t *f, uint32_t now_ms)
{
    int slot = -1;
    for (int i = 0; i < KJ_CLIENT_MAX_ROOMS && slot < 0; i++) {
        if (c->rooms[i].seen_ms != 0 && memcmp(c->rooms[i].mac, mac, 6) == 0) slot = i;
    }
    for (int i = 0; i < KJ_CLIENT_MAX_ROOMS && slot < 0; i++) {
        if (!alive(now_ms, c->rooms[i].seen_ms, KJ_CLIENT_ROOM_TTL_MS)) slot = i;
    }
    if (slot < 0) {
        // 全满且都在有效期内：替换最久没听到的一个。
        slot = 0;
        for (int i = 1; i < KJ_CLIENT_MAX_ROOMS; i++) {
            if ((int32_t)(c->rooms[i].seen_ms - c->rooms[slot].seen_ms) < 0) slot = i;
        }
    }
    kj_room_entry_t *r = &c->rooms[slot];
    memcpy(r->mac, mac, 6);
    r->room = f->room;
    r->phase = f->u.room_info.phase;
    r->seated = f->u.room_info.seated;
    r->seen_ms = now_ms ? now_ms : 1;
}

static void reset_membership(kj_client_t *c)
{
    c->link = KJ_LINK_IDLE;
    c->room = 0;
    memset(c->host_mac, 0, sizeof(c->host_mac));
    c->have_room_info = false;
    c->have_view = false;
    memset(&c->view, 0, sizeof(c->view));
    c->view.my_lock = KJ_CARD_NONE;
    c->view.res_my = c->view.res_opp = KJ_CARD_NONE;
    c->pending = false;
    c->ack_due = false;
}

void kj_client_on_frame(kj_client_t *c, const uint8_t mac[6], const uint8_t *data, size_t len, uint32_t now_ms)
{
    kj_frame_t f;
    if (!kj_proto_decode(data, len, &f)) return;
    bool from_host = c->link != KJ_LINK_IDLE && f.room == c->room && memcmp(mac, c->host_mac, 6) == 0;

    switch (f.type) {
    case KJ_F_ROOM:
        upsert_room(c, mac, &f, now_ms);
        if (from_host) {
            c->room_info = f.u.room_info;
            c->have_room_info = true;
            c->last_host_rx_ms = now_ms;
        }
        break;
    case KJ_F_VIEW: {
        if (!from_host) break;
        c->last_host_rx_ms = now_ms;
        const kj_view_t *v = &f.u.view;
        if (v->no == KJ_NO_NONE) {
            // 庄家不认识我们（被移除 / 赌局重置 / 已满）：回到找赌局。
            if (c->link != KJ_LINK_IDLE) {
                c->kicked = true;
                c->kicked_notice = v->notice;
                reset_membership(c);
            }
            break;
        }
        // 经 UDP 中继时视图可能乱序：同一次庄家开机（epoch）内版本号倒退的是旧视图，丢弃，
        // 但仍回一个心跳，让庄家知道我们手里的版本。庄家重启后 epoch 改变，照常接受。
        if (c->have_view && v->epoch == c->view.epoch && kj_seq_diff(v->view_ver, c->view.view_ver) < 0) {
            c->ack_due = true;
            break;
        }
        if (c->pending && kj_seq_diff(v->ack_seq, c->req.seq) >= 0) c->pending = false;
        bool changed = !c->have_view || memcmp(&c->view, v, sizeof(*v)) != 0;
        c->view = *v;
        c->have_view = true;
        c->view_rx_ms = now_ms;
        c->link = KJ_LINK_JOINED;
        if (changed) c->view_changed = true;
        c->ack_due = true;   // 立即单播心跳确认版本号
        break;
    }
    default:
        break;
    }
}

static void send_req(kj_client_t *c, uint32_t now_ms, kj_outbox_t *out)
{
    kj_frame_t f = { .type = KJ_F_REQ, .room = c->room };
    f.u.req = c->req;
    uint32_t age = now_ms - c->req_t0_ms;
    f.u.req.age_ms = (uint16_t)(age > 0xFFFFu ? 0xFFFFu : age);
    f.u.req.boot = c->boot;
    kj_outbox_push(out, c->host_mac, &f);
    c->req_tries++;
    c->req_next_ms = now_ms + KJ_CLIENT_REQ_RETRY_MS;
}

static void send_hello(kj_client_t *c, const uint8_t *dest, kj_outbox_t *out)
{
    kj_frame_t f = { .type = KJ_F_HELLO, .room = c->link == KJ_LINK_JOINED ? c->room : 0 };
    f.u.hello.no = c->link == KJ_LINK_JOINED ? c->view.no : 0;
    f.u.hello.flags = c->link == KJ_LINK_JOINED ? KJ_HELLO_JOINED : 0;
    f.u.hello.view_ver = c->view.view_ver;
    kj_outbox_push(out, dest, &f);
}

void kj_client_tick(kj_client_t *c, uint32_t now_ms, kj_outbox_t *out)
{
    if (c->pending && (int32_t)(now_ms - c->req_next_ms) >= 0) {
        if (c->req_tries >= KJ_CLIENT_REQ_TRIES) {
            c->pending = false;
            c->req_failed = true;
            if (c->link == KJ_LINK_JOINING) reset_membership(c);   // 入座失败
        } else {
            send_req(c, now_ms, out);
        }
    }
    if (c->ack_due && c->link == KJ_LINK_JOINED) {
        c->ack_due = false;
        send_hello(c, c->host_mac, out);
    }
    if ((int32_t)(now_ms - c->next_hello_ms) >= 0) {
        // 未入座时不发心跳：找赌局只需要听庄家信标。
        if (c->link == KJ_LINK_JOINED) send_hello(c, c->host_mac, out);
        c->next_hello_ms = now_ms + KJ_CLIENT_HELLO_MS - 150 + rnd(c) % 300;
    }
}

int kj_client_rooms(const kj_client_t *c, uint32_t now_ms, kj_room_entry_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < KJ_CLIENT_MAX_ROOMS && n < max; i++) {
        const kj_room_entry_t *r = &c->rooms[i];
        if (!alive(now_ms, r->seen_ms, KJ_CLIENT_ROOM_TTL_MS)) continue;
        int k = n++;
        while (k > 0 && out[k - 1].room > r->room) {
            out[k] = out[k - 1];
            k--;
        }
        out[k] = *r;
    }
    return n;
}

bool kj_client_join(kj_client_t *c, uint16_t room, uint32_t now_ms)
{
    const kj_room_entry_t *found = NULL;
    for (int i = 0; i < KJ_CLIENT_MAX_ROOMS && !found; i++) {
        const kj_room_entry_t *r = &c->rooms[i];
        if (r->room == room && alive(now_ms, r->seen_ms, KJ_CLIENT_ROOM_TTL_MS)) found = r;
    }
    if (!found) return false;
    reset_membership(c);
    c->link = KJ_LINK_JOINING;
    c->room = room;
    memcpy(c->host_mac, found->mac, 6);
    c->last_host_rx_ms = now_ms;
    c->req.seq = ++c->seq;
    c->req.op = KJ_OP_JOIN;
    c->req.arg = 0;
    c->pending = true;
    c->req_t0_ms = now_ms;
    c->req_tries = 0;
    c->req_next_ms = now_ms;
    c->req_failed = false;
    return true;
}

void kj_client_leave(kj_client_t *c)
{
    reset_membership(c);
}

bool kj_client_request(kj_client_t *c, uint8_t op, uint8_t arg, uint32_t now_ms)
{
    if (c->link != KJ_LINK_JOINED || c->pending || op == KJ_OP_JOIN) return false;
    c->req.seq = ++c->seq;
    c->req.op = op;
    c->req.arg = arg;
    c->pending = true;
    c->req_t0_ms = now_ms;
    c->req_tries = 0;
    c->req_next_ms = now_ms;   // 下一次 tick 立即发送
    return true;
}

bool kj_client_connected(const kj_client_t *c, uint32_t now_ms)
{
    return c->link != KJ_LINK_IDLE && alive(now_ms, c->last_host_rx_ms ? c->last_host_rx_ms : 1,
                                            KJ_CLIENT_HOST_TTL_MS);
}

static bool better(const kj_opponent_t *a, const kj_opponent_t *b)
{
    if (a->is_bot != b->is_bot) return a->is_bot < b->is_bot;
    return a->no < b->no;
}

int kj_client_opponents(const kj_client_t *c, kj_opponent_t *out, int max)
{
    if (c->link != KJ_LINK_JOINED || !c->have_room_info || max <= 0) return 0;
    int n = 0;
    for (int no = 1; no <= KJ_MAX_PLAYERS; no++) {
        if (no == c->view.no || !kj_bit_get(c->room_info.avail, (uint8_t)no)) continue;
        kj_opponent_t o = { .no = (uint8_t)no, .is_bot = kj_bit_get(c->room_info.bots, (uint8_t)no) };
        // 有界插入排序：只保留最好的 max 个（人多时真人不会因编号大被电脑选手挤掉）。
        int k;
        if (n < max) {
            k = n++;
        } else if (better(&o, &out[max - 1])) {
            k = max - 1;
        } else {
            continue;
        }
        while (k > 0 && better(&o, &out[k - 1])) {
            out[k] = out[k - 1];
            k--;
        }
        out[k] = o;
    }
    return n;
}

bool kj_client_take_view_changed(kj_client_t *c)
{
    bool v = c->view_changed;
    c->view_changed = false;
    return v;
}

bool kj_client_take_req_failed(kj_client_t *c)
{
    bool v = c->req_failed;
    c->req_failed = false;
    return v;
}

bool kj_client_take_kicked(kj_client_t *c, uint8_t *notice)
{
    if (!c->kicked) return false;
    c->kicked = false;
    if (notice) *notice = c->kicked_notice;
    c->kicked_notice = 0;
    return true;
}
