// main/kj_hubc.c —— 设备侧 hub 客户端（纯 C）。
#include "kj_hubc.h"
#include "kj_proto.h"

#include <string.h>

static void copy_name(char *dst, const char *src)
{
    size_t n = 0;
    if (src) {
        while (src[n] && n < KJ_NAME_MAX) n++;
    }
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
}

void kj_hubc_init(kj_hubc_t *h, const uint8_t mac[6], uint16_t boot, const char *fw, const char *my_name,
                  uint32_t my_rev, uint32_t hint_ip)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->mac, mac, 6);
    h->boot = boot;
    size_t n = 0;
    while (fw && fw[n] && n < KH_FW_LEN) {
        h->fw[n] = fw[n];
        n++;
    }
    copy_name(h->my_name, my_name);
    h->my_rev = my_rev;
    h->hint_ip = hint_ip;
    h->discover_now = true;
}

void kj_hubc_set_role(kj_hubc_t *h, uint8_t role, uint16_t room)
{
    if (h->role != role || h->room != room) h->discover_now = true;   // 让 hub 尽快知道
    h->role = role;
    h->room = room;
}

void kj_hubc_note_rx(kj_hubc_t *h, uint32_t now_ms)
{
    if (h->state == KJ_HUB_OK) h->last_rx_ms = now_ms;
}

kj_hub_state_t kj_hubc_state(const kj_hubc_t *h, uint32_t now_ms)
{
    if (h->state == KJ_HUB_OK && (uint32_t)(now_ms - h->last_rx_ms) < KJ_HUBC_LOST_MS) return KJ_HUB_OK;
    return KJ_HUB_SEARCHING;
}

static void clear_names(kj_hubc_t *h)
{
    for (int i = 0; i < KJ_HUBC_NAME_CACHE; i++) h->names[i].valid = false;
}

static void store_name(kj_hubc_t *h, uint16_t room, const kh_name_entry_t *e, uint32_t now_ms)
{
    int slot = -1;
    for (int i = 0; i < KJ_HUBC_NAME_CACHE && slot < 0; i++) {
        const kj_name_slot_t *s = &h->names[i];
        if (s->valid && s->room == room && s->no == e->no) slot = i;
    }
    for (int i = 0; i < KJ_HUBC_NAME_CACHE && slot < 0; i++) {
        if (!h->names[i].valid) slot = i;
    }
    if (slot < 0) {   // 满了：替换最久没用的
        slot = 0;
        for (int i = 1; i < KJ_HUBC_NAME_CACHE; i++) {
            if ((int32_t)(h->names[i].used_ms - h->names[slot].used_ms) < 0) slot = i;
        }
    }
    kj_name_slot_t *s = &h->names[slot];
    s->valid = true;
    s->room = room;
    s->no = e->no;
    s->flags = e->flags;
    s->used_ms = now_ms;
    copy_name(s->name, e->name);
}

void kj_hubc_on_msg(kj_hubc_t *h, const kh_env_t *e, uint32_t src_ip, uint32_t now_ms)
{
    // 找到 hub 之后只认它：同一网络里另一台电脑也在跑 hub 时不会来回切换。
    if (kj_hubc_state(h, now_ms) == KJ_HUB_OK && src_ip != h->hub_ip) return;
    switch (e->kind) {
    case KH_K_OFFER: {
        kh_offer_t o;
        if (!kh_offer_decode(e->payload, e->len, &o)) return;
        bool found = kj_hubc_state(h, now_ms) != KJ_HUB_OK;
        if (found || h->hub_id != o.hub_id || h->hub_ip != src_ip) {
            clear_names(h);          // 新找到 / 换了 hub：缓存的昵称不再可信
            h->want_sent = false;
        }
        if (found) h->next_discover_ms = now_ms + KJ_HUBC_KEEPALIVE_MS;   // 刚找到：下次保活 5 s 后
        h->state = KJ_HUB_OK;
        h->hub_ip = src_ip;
        h->hint_ip = src_ip;
        h->hub_id = o.hub_id;
        h->http_port = o.http_port;
        h->tcp_port = o.tcp_port;
        h->last_rx_ms = now_ms;
        h->incompatible = (o.flags & KH_OFFER_INCOMPATIBLE) != 0;
        if (o.roster_rev != h->roster_rev) {
            h->roster_rev = o.roster_rev;
            clear_names(h);
        }
        if ((o.flags & KH_OFFER_NAME_VALID) && (o.name_rev != h->my_rev || strcmp(o.name, h->my_name) != 0)) {
            copy_name(h->my_name, o.name);
            h->my_rev = o.name_rev;
            h->my_changed = true;
        }
        break;
    }
    case KH_K_REG_STATE: {
        kh_reg_state_msg_t m;
        if (!h->reg_active || !kh_reg_state_decode(e->payload, e->len, &m)) return;
        if (strncmp(m.token, h->token, KJ_REG_TOKEN_LEN) != 0) return;
        h->reg_state = m.state;
        if (m.state == KH_REG_DONE && (m.name_rev != h->my_rev || strcmp(m.name, h->my_name) != 0)) {
            copy_name(h->my_name, m.name);
            h->my_rev = m.name_rev;
            h->my_changed = true;
        }
        break;
    }
    case KH_K_NAMES: {
        kh_names_t m;
        if (!kh_names_decode(e->payload, e->len, &m)) return;
        if (m.roster_rev != h->roster_rev) {
            h->roster_rev = m.roster_rev;
            clear_names(h);
        }
        for (int i = 0; i < m.count; i++) {
            store_name(h, e->room, &m.e[i], now_ms);
            if (e->room != h->want_room) continue;
            for (int k = 0; k < h->want_n; k++) {   // 已回答的从待查询里删掉
                if (h->want[k] == m.e[i].no) {
                    h->want[k] = h->want[--h->want_n];
                    break;
                }
            }
        }
        if (h->want_n == 0) h->want_sent = false;
        break;
    }
    default:
        break;
    }
}

bool kj_hubc_name(kj_hubc_t *h, uint16_t room, uint8_t no, uint32_t now_ms, const char **name, uint8_t *flags)
{
    for (int i = 0; i < KJ_HUBC_NAME_CACHE; i++) {
        kj_name_slot_t *s = &h->names[i];
        if (s->valid && s->room == room && s->no == no) {
            s->used_ms = now_ms;
            if (name) *name = s->name;
            if (flags) *flags = s->flags;
            return true;
        }
    }
    if (name) *name = NULL;
    if (flags) *flags = KH_NAME_UNKNOWN;
    if (no == 0) return false;
    if (room != h->want_room) {   // 换了赌局：之前的待查询作废
        h->want_room = room;
        h->want_n = 0;
        h->want_sent = false;
    }
    for (int k = 0; k < h->want_n; k++) {
        if (h->want[k] == no) return false;
    }
    if (h->want_n < KH_NAMES_MAX) h->want[h->want_n++] = no;
    return false;
}

static int push_out(kj_hubc_out_t *out, int n, int max, uint8_t kind, uint32_t ip, uint16_t room,
                    const uint8_t *data, size_t len)
{
    if (n >= max || len == 0 || len > sizeof(out[0].data)) return n;
    out[n].kind = kind;
    out[n].ip = ip;
    out[n].room = room;
    out[n].len = (uint16_t)len;
    memcpy(out[n].data, data, len);
    return n + 1;
}

int kj_hubc_tick(kj_hubc_t *h, uint32_t now_ms, kj_hubc_out_t *out, int max)
{
    int n = 0;
    bool ok = kj_hubc_state(h, now_ms) == KJ_HUB_OK;
    if (!ok && h->state == KJ_HUB_OK) {
        h->state = KJ_HUB_SEARCHING;   // 失联：回到寻找，马上发一次
        h->discover_now = true;
    }
    if (h->discover_now || h->next_discover_ms == 0 || (int32_t)(now_ms - h->next_discover_ms) >= 0) {
        kh_discover_t d = { .hub_proto = KH_VERSION, .game_proto = KJ_PROTO_VERSION, .role = h->role,
                            .boot = h->boot, .name_rev = h->my_rev };
        memcpy(d.fw, h->fw, sizeof(d.fw));
        copy_name(d.name, h->my_name);
        uint8_t buf[64];
        size_t len = kh_discover_encode(&d, buf, sizeof(buf));
        uint32_t ip;
        if (ok) {
            ip = h->hub_ip;
        } else {
            // 寻找中：有上次的地址就和广播交替发（应对路由器不转发广播的网络）
            h->alt = !h->alt;
            ip = (h->hint_ip && h->alt) ? h->hint_ip : 0;
        }
        n = push_out(out, n, max, KH_K_DISCOVER, ip, h->room, buf, len);
        h->discover_now = false;
        h->next_discover_ms = now_ms + (ok ? KJ_HUBC_KEEPALIVE_MS : KJ_HUBC_DISCOVER_MS);
    }
    if (!ok) return n;
    if (h->want_n > 0 && (!h->want_sent || (uint32_t)(now_ms - h->want_sent_ms) >= KJ_HUBC_NAME_RETRY_MS)) {
        uint8_t buf[KH_NAMES_MAX + 1];
        size_t len = kh_name_get_encode(h->want, h->want_n, buf, sizeof(buf));
        int before = n;
        n = push_out(out, n, max, KH_K_NAME_GET, h->hub_ip, h->want_room, buf, len);
        if (n > before) {
            h->want_sent = true;
            h->want_sent_ms = now_ms;
        }
    }
    if (h->reg_active && (int32_t)(now_ms - h->next_reg_ms) >= 0) {
        uint8_t buf[KJ_REG_TOKEN_LEN];
        size_t len = kh_reg_encode(h->token, buf, sizeof(buf));
        n = push_out(out, n, max, KH_K_REG, h->hub_ip, h->room, buf, len);
        h->next_reg_ms = now_ms + KJ_HUBC_REG_MS;
    }
    return n;
}

void kj_hubc_reg_start(kj_hubc_t *h, const char token[KJ_REG_TOKEN_LEN], uint32_t now_ms)
{
    h->reg_active = true;
    memcpy(h->token, token, KJ_REG_TOKEN_LEN);
    h->token[KJ_REG_TOKEN_LEN] = '\0';
    h->reg_state = KH_REG_WAITING;
    h->next_reg_ms = now_ms;
}

void kj_hubc_reg_stop(kj_hubc_t *h)
{
    h->reg_active = false;
    h->reg_state = KH_REG_INVALID;
}

bool kj_hubc_take_my_name_changed(kj_hubc_t *h)
{
    bool v = h->my_changed;
    h->my_changed = false;
    return v;
}

void kj_reg_token(uint32_t r1, uint32_t r2, char out[KJ_REG_TOKEN_LEN + 1])
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    uint64_t bits = ((uint64_t)(r2 & 0xFF) << 32) | r1;   // 40 位
    for (int i = 0; i < KJ_REG_TOKEN_LEN; i++) {
        out[i] = alphabet[bits & 31u];
        bits >>= 5;
    }
    out[KJ_REG_TOKEN_LEN] = '\0';
}
