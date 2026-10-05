// main/kj_hubproto.c —— 设备 ⇄ hub UDP 线协议编解码（纯 C，固定小端布局）。
#include "kj_hubproto.h"
#include "kj_utf8.h"

#include <string.h>

const uint8_t KH_MAC_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
const uint8_t KH_MAC_HUB[6] = { 0, 0, 0, 0, 0, 0 };

typedef struct {
    uint8_t *p;
    size_t n, cap;
    bool ok;
} wr_t;

static void w8(wr_t *w, uint8_t v)
{
    if (w->n + 1 > w->cap) {
        w->ok = false;
        return;
    }
    w->p[w->n++] = v;
}

static void w16(wr_t *w, uint16_t v)
{
    w8(w, (uint8_t)v);
    w8(w, (uint8_t)(v >> 8));
}

static void w32(wr_t *w, uint32_t v)
{
    for (int i = 0; i < 4; i++) w8(w, (uint8_t)(v >> (8 * i)));
}

static void wn(wr_t *w, const void *src, size_t n)
{
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) w8(w, s[i]);
}

typedef struct {
    const uint8_t *p;
    size_t n, len;
    bool ok;
} rd_t;

static uint8_t r8(rd_t *r)
{
    if (r->n + 1 > r->len) {
        r->ok = false;
        return 0;
    }
    return r->p[r->n++];
}

static uint16_t r16(rd_t *r)
{
    uint16_t lo = r8(r);
    return (uint16_t)(lo | ((uint16_t)r8(r) << 8));
}

static uint32_t r32(rd_t *r)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)r8(r) << (8 * i);
    return v;
}

static const uint8_t *rn(rd_t *r, size_t n)
{
    if (r->n + n > r->len) {
        r->ok = false;
        return NULL;
    }
    const uint8_t *p = r->p + r->n;
    r->n += n;
    return p;
}

// 名字：len u8 + 字节；写入前清理 / 截断，读出后清理（不信任对端）。
static void w_name(wr_t *w, const char *name)
{
    char clean[KJ_NAME_MAX + 1];
    size_t n = name ? kj_utf8_sanitize(name, strlen(name), clean, sizeof(clean)) : 0;
    w8(w, (uint8_t)n);
    wn(w, clean, n);
}

static void r_name(rd_t *r, char out[KJ_NAME_MAX + 1])
{
    uint8_t n = r8(r);
    const uint8_t *s = rn(r, n);
    out[0] = '\0';
    if (s && n <= KJ_NAME_MAX) {
        kj_utf8_sanitize((const char *)s, n, out, KJ_NAME_MAX + 1);
    } else if (s) {
        r->ok = false;   // 超长：协议错误
    }
}

size_t kh_env_encode(const kh_env_t *e, uint8_t *buf, size_t cap)
{
    if (e->kind == 0 || e->kind >= KH_K_COUNT || e->len > KH_PAYLOAD_MAX) return 0;
    if (e->len && !e->payload) return 0;
    wr_t w = { .p = buf, .cap = cap, .ok = true };
    w8(&w, 'K');
    w8(&w, 'H');
    w8(&w, KH_VERSION);
    w8(&w, e->kind);
    wn(&w, e->src, 6);
    wn(&w, e->dst, 6);
    w16(&w, e->room);
    w16(&w, e->seq);
    w8(&w, (uint8_t)e->rssi);
    w8(&w, e->flags);
    w16(&w, e->len);
    wn(&w, e->payload, e->len);
    return w.ok ? w.n : 0;
}

bool kh_env_decode(const uint8_t *buf, size_t len, kh_env_t *out)
{
    if (!buf || !out || len < KH_HDR) return false;
    if (buf[0] != 'K' || buf[1] != 'H' || buf[2] != KH_VERSION) return false;
    rd_t r = { .p = buf, .n = 3, .len = len, .ok = true };
    memset(out, 0, sizeof(*out));
    out->kind = r8(&r);
    memcpy(out->src, rn(&r, 6), 6);
    memcpy(out->dst, rn(&r, 6), 6);
    out->room = r16(&r);
    out->seq = r16(&r);
    out->rssi = (int8_t)r8(&r);
    out->flags = r8(&r);
    out->len = r16(&r);
    if (out->kind == 0 || out->kind >= KH_K_COUNT) return false;
    if (out->len > KH_PAYLOAD_MAX || (size_t)KH_HDR + out->len != len) return false;
    out->payload = buf + KH_HDR;
    return true;
}

size_t kh_discover_encode(const kh_discover_t *m, uint8_t *buf, size_t cap)
{
    wr_t w = { .p = buf, .cap = cap, .ok = true };
    w8(&w, m->hub_proto);
    w8(&w, m->game_proto);
    w8(&w, m->role);
    w16(&w, m->boot);
    char fw[KH_FW_LEN] = { 0 };
    for (size_t i = 0; i < KH_FW_LEN && m->fw[i]; i++) fw[i] = m->fw[i];
    wn(&w, fw, KH_FW_LEN);
    w32(&w, m->name_rev);
    w_name(&w, m->name);
    return w.ok ? w.n : 0;
}

bool kh_discover_decode(const uint8_t *p, size_t n, kh_discover_t *out)
{
    rd_t r = { .p = p, .len = n, .ok = true };
    memset(out, 0, sizeof(*out));
    out->hub_proto = r8(&r);
    out->game_proto = r8(&r);
    out->role = r8(&r);
    out->boot = r16(&r);
    const uint8_t *fw = rn(&r, KH_FW_LEN);
    if (fw) {
        for (size_t i = 0; i < KH_FW_LEN && fw[i]; i++) out->fw[i] = (fw[i] >= 0x20 && fw[i] < 0x7F) ? (char)fw[i] : '?';
    }
    out->name_rev = r32(&r);
    r_name(&r, out->name);
    return r.ok && out->role <= KH_ROLE_HOST;
}

size_t kh_offer_encode(const kh_offer_t *m, uint8_t *buf, size_t cap)
{
    wr_t w = { .p = buf, .cap = cap, .ok = true };
    w32(&w, m->hub_id);
    w16(&w, m->http_port);
    w16(&w, m->tcp_port);
    w32(&w, m->roster_rev);
    w8(&w, m->flags);
    w32(&w, m->name_rev);
    w_name(&w, m->name);
    return w.ok ? w.n : 0;
}

bool kh_offer_decode(const uint8_t *p, size_t n, kh_offer_t *out)
{
    rd_t r = { .p = p, .len = n, .ok = true };
    memset(out, 0, sizeof(*out));
    out->hub_id = r32(&r);
    out->http_port = r16(&r);
    out->tcp_port = r16(&r);
    out->roster_rev = r32(&r);
    out->flags = r8(&r);
    out->name_rev = r32(&r);
    r_name(&r, out->name);
    return r.ok && out->http_port && out->tcp_port;
}

bool kh_token_valid(const char *token)
{
    if (!token) return false;
    for (int i = 0; i < KJ_REG_TOKEN_LEN; i++) {
        char c = token[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '7'))) return false;
    }
    return true;
}

size_t kh_reg_encode(const char token[KJ_REG_TOKEN_LEN], uint8_t *buf, size_t cap)
{
    if (!kh_token_valid(token) || cap < KJ_REG_TOKEN_LEN) return 0;
    memcpy(buf, token, KJ_REG_TOKEN_LEN);
    return KJ_REG_TOKEN_LEN;
}

bool kh_reg_decode(const uint8_t *p, size_t n, char token[KJ_REG_TOKEN_LEN + 1])
{
    if (n != KJ_REG_TOKEN_LEN) return false;
    memcpy(token, p, KJ_REG_TOKEN_LEN);
    token[KJ_REG_TOKEN_LEN] = '\0';
    return kh_token_valid(token);
}

size_t kh_reg_state_encode(const kh_reg_state_msg_t *m, uint8_t *buf, size_t cap)
{
    if (!kh_token_valid(m->token)) return 0;
    wr_t w = { .p = buf, .cap = cap, .ok = true };
    wn(&w, m->token, KJ_REG_TOKEN_LEN);
    w8(&w, m->state);
    w32(&w, m->name_rev);
    w_name(&w, m->name);
    return w.ok ? w.n : 0;
}

bool kh_reg_state_decode(const uint8_t *p, size_t n, kh_reg_state_msg_t *out)
{
    rd_t r = { .p = p, .len = n, .ok = true };
    memset(out, 0, sizeof(*out));
    const uint8_t *t = rn(&r, KJ_REG_TOKEN_LEN);
    if (t) memcpy(out->token, t, KJ_REG_TOKEN_LEN);
    out->state = r8(&r);
    out->name_rev = r32(&r);
    r_name(&r, out->name);
    return r.ok && kh_token_valid(out->token) && out->state <= KH_REG_DONE;
}

size_t kh_name_get_encode(const uint8_t *nos, int count, uint8_t *buf, size_t cap)
{
    if (count < 1 || count > KH_NAMES_MAX) return 0;
    wr_t w = { .p = buf, .cap = cap, .ok = true };
    w8(&w, (uint8_t)count);
    wn(&w, nos, (size_t)count);
    return w.ok ? w.n : 0;
}

int kh_name_get_decode(const uint8_t *p, size_t n, uint8_t nos[KH_NAMES_MAX])
{
    if (n < 1 || p[0] < 1 || p[0] > KH_NAMES_MAX || n != (size_t)p[0] + 1) return -1;
    for (int i = 0; i < p[0]; i++) {
        if (p[1 + i] == 0) return -1;
        nos[i] = p[1 + i];
    }
    return p[0];
}

size_t kh_names_encode(const kh_names_t *m, uint8_t *buf, size_t cap)
{
    if (m->count > KH_NAMES_MAX) return 0;
    wr_t w = { .p = buf, .cap = cap, .ok = true };
    w32(&w, m->roster_rev);
    w8(&w, m->count);
    for (int i = 0; i < m->count; i++) {
        w8(&w, m->e[i].no);
        w8(&w, m->e[i].flags);
        w_name(&w, m->e[i].name);
    }
    return w.ok ? w.n : 0;
}

bool kh_names_decode(const uint8_t *p, size_t n, kh_names_t *out)
{
    rd_t r = { .p = p, .len = n, .ok = true };
    memset(out, 0, sizeof(*out));
    out->roster_rev = r32(&r);
    out->count = r8(&r);
    if (out->count > KH_NAMES_MAX) return false;
    for (int i = 0; i < out->count; i++) {
        out->e[i].no = r8(&r);
        out->e[i].flags = r8(&r);
        r_name(&r, out->e[i].name);
        if (out->e[i].no == 0) r.ok = false;
    }
    return r.ok && r.n == n;
}
