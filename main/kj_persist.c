// main/kj_persist.c —— 赌局状态序列化（小端，末尾 32 位校验和）。
#include "kj_persist.h"

#include <string.h>

static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void w32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t r32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t checksum(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;   // FNV-1a
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

size_t kj_persist_save(const kj_game_t *g, uint8_t *buf, size_t cap)
{
    int count = kj_rules_player_count(g);
    size_t need = KJ_PERSIST_HEADER + (size_t)count * KJ_PERSIST_RECORD + 4;
    if (cap < need) return 0;
    w32(buf, KJ_PERSIST_MAGIC);
    buf[4] = KJ_PERSIST_VERSION;
    buf[5] = g->phase;
    w16(buf + 6, g->game_id);
    w16(buf + 8, g->next_duel_id);
    w16(buf + 10, g->phase_ver);
    buf[12] = (uint8_t)count;   // ≤ 128
    buf[13] = 0;
    uint8_t *q = buf + KJ_PERSIST_HEADER;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        const kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        q[0] = (uint8_t)i;
        q[1] = p->is_bot ? 1 : 0;
        memcpy(q + 2, p->mac, 6);
        q[8] = p->status;
        memcpy(q + 9, p->cards, KJ_CARD_TYPES);
        q[12] = p->stars;
        q[13] = p->wins;
        q[14] = p->losses;
        q[15] = p->draws;
        q[16] = p->final_reason;
        q[17] = 0;
        w16(q + 18, p->view_ver);
        q += KJ_PERSIST_RECORD;
    }
    w32(q, checksum(buf, (size_t)(q - buf)));
    return need;
}

bool kj_persist_load(kj_game_t *g, const uint8_t *buf, size_t len, uint32_t now_ms)
{
    if (len < KJ_PERSIST_HEADER + 4) return false;
    if (r32(buf) != KJ_PERSIST_MAGIC || buf[4] != KJ_PERSIST_VERSION) return false;
    int count = buf[12];
    if (count > KJ_MAX_PLAYERS) return false;
    size_t need = KJ_PERSIST_HEADER + (size_t)count * KJ_PERSIST_RECORD + 4;
    if (len != need) return false;
    if (r32(buf + need - 4) != checksum(buf, need - 4)) return false;
    if (buf[5] > KJ_PHASE_ENDED) return false;

    // 第一遍只校验，全部通过才写入调用方状态（失败时 g 保持原样）。
    const uint8_t *q = buf + KJ_PERSIST_HEADER;
    uint8_t seen[KJ_MAX_PLAYERS / 8] = { 0 };
    for (int k = 0; k < count; k++, q += KJ_PERSIST_RECORD) {
        int i = q[0];
        if (i >= KJ_MAX_PLAYERS || (seen[i / 8] >> (i % 8)) & 1u) return false;
        seen[i / 8] |= (uint8_t)(1u << (i % 8));
        if (q[8] >= KJ_ST_COUNT || q[16] > KJ_FINAL_TIME_UP) return false;
        for (int c = 0; c < KJ_CARD_TYPES; c++) {
            if (q[9 + c] > KJ_CARDS_PER_TYPE) return false;
        }
    }

    kj_rules_init(g);
    g->phase = buf[5];
    g->game_id = r16(buf + 6);
    g->next_duel_id = r16(buf + 8) ? r16(buf + 8) : 1;
    g->phase_ver = (uint16_t)(r16(buf + 10) + 1);
    g->phase_since_ms = now_ms;
    q = buf + KJ_PERSIST_HEADER;
    for (int k = 0; k < count; k++, q += KJ_PERSIST_RECORD) {
        kj_player_t *p = &g->players[q[0]];
        p->used = 1;
        p->is_bot = q[1] & 1;
        memcpy(p->mac, q + 2, 6);
        memcpy(p->cards, q + 9, KJ_CARD_TYPES);
        p->stars = q[12];
        p->wins = q[13];
        p->losses = q[14];
        p->draws = q[15];
        p->final_reason = q[16];
        uint8_t st = q[8];
        // 进行中的挑战 / 对决不恢复：暗牌未扣，双方回到空闲。
        if (st == KJ_ST_CHALLENGING || st == KJ_ST_CHALLENGED || st == KJ_ST_DUEL) st = KJ_ST_IDLE;
        p->status = st;
        p->view_ver = (uint16_t)(r16(q + 18) + KJ_PERSIST_VER_BUMP);
        p->since_ms = now_ms;
        // 真人选手等收到心跳才算在线；电脑选手始终在线。
        p->last_seen_ms = now_ms - KJ_ONLINE_TIMEOUT_MS;
        p->online = p->is_bot ? 1 : 0;
    }
    kj_rules_mark_all_dirty(g);
    return true;
}
