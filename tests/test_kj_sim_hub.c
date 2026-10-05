// tests/test_kj_sim_hub.c —— 经电脑 hub 中继的联机仿真：1 台庄家 + 12 台选手 + 电脑选手，所有帧都套上 hub 信封
// （kh_env_encode / decode），先到 hub 再按 MAC 转发（与 tools/kj_hub/core.py 相同的路由规则）。
// 每一跳都有丢包、重复与随机延迟（因而会乱序）；中途重启一台选手、重启 hub（路由表清空）、重启庄家（快照恢复，
// epoch 改变）。检查：星星守恒、手牌与结算次数一致、挑战 / 对决 / 碰拳配对关系对称、同一 epoch 内选手接受的
// 视图版本不倒退、两人都按下的碰拳在丢包下仍大多配对成功、安静期后所有选手视图与庄家一致。
#include "kj_bump.h"
#include "kj_client.h"
#include "kj_hubproto.h"
#include "kj_persist.h"
#include "kj_server.h"
#include "kj_test.h"

#include <string.h>

#define HUMANS 12
#define BOTS 2
#define STEP_MS 10
#define ROOM_ID 0x4B48
#define HOST_NODE HUMANS          // 节点编号：0..HUMANS-1 是选手，HUMANS 是庄家
#define NODES (HUMANS + 1)
#define HUB_NODE (-1)
#define MAX_PKTS 8192
#define MAX_GROUPS 512

typedef struct {
    uint32_t at;
    int to;                       // 节点编号或 HUB_NODE
    uint16_t len;
    uint8_t data[KH_HDR + KJ_FRAME_MAX];
} pkt_t;

typedef struct {
    kj_client_t c;
    uint8_t mac[6];
    uint32_t next_action_ms;
    uint32_t bump_at;
    int bump_group;
    uint16_t seen_epoch, seen_ver;
    bool seen_any;
} sim_player_t;

typedef struct {
    int size, sent;
    int member[3];
    bool matched;
} bump_group_t;

static kj_server_t server;
static sim_player_t players[HUMANS];
static const uint8_t host_mac[6] = { 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC };
static bool hub_knows[NODES];     // hub 的路由表：知道哪些节点的地址
static pkt_t heap[MAX_PKTS];
static int heap_n;
static uint32_t now_ms = 1, rng = 0x12345678u;
static int loss_pct = 10, dup_pct = 1;
static bool actions_enabled = true;
static long results_seen, matches_seen, delivered, dropped, overflow;
static bump_group_t groups[MAX_GROUPS];
static int group_count, dealt_players;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

// ---------------------------------------------------------------------------
// 网络：按到达时间排序的最小堆
// ---------------------------------------------------------------------------

static void heap_push(const pkt_t *p)
{
    if (heap_n >= MAX_PKTS) {
        overflow++;
        return;
    }
    int i = heap_n++;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if ((int32_t)(heap[parent].at - p->at) <= 0) break;
        heap[i] = heap[parent];
        i = parent;
    }
    heap[i] = *p;
}

static pkt_t heap_pop(void)
{
    pkt_t top = heap[0];
    pkt_t last = heap[--heap_n];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        const pkt_t *mp = &last;
        if (l < heap_n && (int32_t)(heap[l].at - mp->at) < 0) {
            m = l;
            mp = &heap[l];
        }
        if (r < heap_n && (int32_t)(heap[r].at - mp->at) < 0) m = r;
        if (m == i) break;
        heap[i] = heap[m];
        i = m;
    }
    if (heap_n > 0) heap[i] = last;
    return top;
}

static uint32_t hop_delay(void)
{
    uint32_t d = 3 + rnd() % 16;              // 3~18 ms 抖动：同一发送方的包也会乱序
    if (rnd() % 100 < 2) d += 150;            // 偶尔卡一下
    return d;
}

// 发一跳：丢包、重复、延迟。
static void hop(int to, const uint8_t *data, uint16_t len)
{
    int copies = 1 + ((int)(rnd() % 100) < dup_pct ? 1 : 0);
    for (int k = 0; k < copies; k++) {
        if ((int)(rnd() % 100) < loss_pct) {
            dropped++;
            continue;
        }
        pkt_t p = { .at = now_ms + hop_delay(), .to = to, .len = len };
        memcpy(p.data, data, len);
        heap_push(&p);
    }
}

static int node_of_mac(const uint8_t mac[6])
{
    if (memcmp(mac, host_mac, 6) == 0) return HOST_NODE;
    for (int i = 0; i < HUMANS; i++) {
        if (memcmp(mac, players[i].mac, 6) == 0) return i;
    }
    return -2;
}

// 设备发出的帧：套上 hub 信封，第一跳到 hub。
static void send_from(int node, const kj_outbox_t *o)
{
    const uint8_t *src = node == HOST_NODE ? host_mac : players[node].mac;
    for (int k = 0; k < o->count; k++) {
        const kj_out_t *it = &o->items[k];
        kh_env_t e = { .kind = KH_K_FRAME, .room = ROOM_ID, .rssi = -55, .payload = it->data, .len = it->len,
                       .flags = node == HOST_NODE ? KH_FLAG_HOST : 0 };
        memcpy(e.src, src, 6);
        memcpy(e.dst, it->broadcast ? KH_MAC_BROADCAST : it->mac, 6);
        uint8_t buf[KH_HDR + KJ_FRAME_MAX];
        size_t n = kh_env_encode(&e, buf, sizeof(buf));
        CHECK(n > 0);
        hop(HUB_NODE, buf, (uint16_t)n);
    }
}

// hub：学习来源地址，按 tools/kj_hub/core.py 的规则转发。
static void hub_route(const pkt_t *p)
{
    kh_env_t e;
    if (!kh_env_decode(p->data, p->len, &e) || e.kind != KH_K_FRAME) {
        CHECK(false);
        return;
    }
    int src = node_of_mac(e.src);
    if (src < 0) return;
    hub_knows[src] = true;
    if (memcmp(e.dst, KH_MAC_BROADCAST, 6) == 0) {
        if (e.flags & KH_FLAG_HOST) {
            for (int i = 0; i < HUMANS; i++) {
                if (hub_knows[i]) hop(i, p->data, p->len);
            }
        } else if (hub_knows[HOST_NODE]) {
            hop(HOST_NODE, p->data, p->len);
        }
    } else {
        int dst = node_of_mac(e.dst);
        if (dst >= 0 && hub_knows[dst]) hop(dst, p->data, p->len);
    }
}

static void deliver(const pkt_t *p)
{
    delivered++;
    if (p->to == HUB_NODE) {
        hub_route(p);
        return;
    }
    kh_env_t e;
    if (!kh_env_decode(p->data, p->len, &e)) {
        CHECK(false);
        return;
    }
    if (p->to == HOST_NODE) {
        kj_outbox_t reply;
        kj_outbox_clear(&reply);
        kj_server_on_frame(&server, e.src, e.rssi, e.payload, e.len, now_ms, &reply);
        send_from(HOST_NODE, &reply);
    } else {
        kj_client_on_frame(&players[p->to].c, e.src, e.payload, e.len, now_ms);
    }
}

// ---------------------------------------------------------------------------
// 选手行为
// ---------------------------------------------------------------------------

static bool free_for_bump(int i)
{
    const sim_player_t *sp = &players[i];
    return sp->bump_at == 0 && sp->c.link == KJ_LINK_JOINED && !sp->c.pending &&
           sp->c.view.phase == KJ_PHASE_RUNNING && sp->c.view.status == KJ_ST_IDLE;
}

static void schedule_bumps(void)
{
    if (!actions_enabled || now_ms % 1500 >= STEP_MS || group_count >= MAX_GROUPS) return;
    uint32_t r = rnd() % 100;
    int want = r < 60 ? 2 : r < 70 ? 3 : r < 80 ? 1 : 0;
    if (want == 0) return;
    bump_group_t *g = &groups[group_count];
    memset(g, 0, sizeof(*g));
    for (int tries = 0; tries < 40 && g->size < want; tries++) {
        int i = (int)(rnd() % HUMANS);
        if (!free_for_bump(i)) continue;
        // 两人按下时刻相差约 N(0, 120 ms)：用三个均匀分布的和近似
        players[i].bump_at = now_ms + 1 + (rnd() % 140 + rnd() % 140 + rnd() % 140);
        players[i].bump_group = group_count;
        g->member[g->size++] = i;
    }
    if (g->size) group_count++;
}

static void act(int i)
{
    sim_player_t *sp = &players[i];
    kj_client_t *c = &sp->c;
    const kj_view_t *v = &c->view;
    if (sp->bump_at && actions_enabled && (int32_t)(now_ms - sp->bump_at) >= 0) {
        sp->bump_at = 0;
        if (c->link == KJ_LINK_JOINED && v->phase == KJ_PHASE_RUNNING && v->status == KJ_ST_IDLE &&
            kj_client_request(c, KJ_OP_BUMP, 0, now_ms)) {
            groups[sp->bump_group].sent++;
        }
        return;
    }
    if ((int32_t)(now_ms - sp->next_action_ms) < 0) return;
    sp->next_action_ms = now_ms + 300 + rnd() % 1500;
    if (c->link == KJ_LINK_IDLE) {
        kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];
        if (kj_client_rooms(c, now_ms, rooms, KJ_CLIENT_MAX_ROOMS) > 0) kj_client_join(c, rooms[0].room, now_ms);
        return;
    }
    if (!actions_enabled || c->link != KJ_LINK_JOINED || c->pending || sp->bump_at) return;
    if (v->phase != KJ_PHASE_RUNNING) return;
    uint32_t r = rnd() % 100;
    switch (v->status) {
    case KJ_ST_IDLE: {
        kj_opponent_t opp[8];
        int n = kj_client_opponents(c, opp, 8);
        if (n > 0 && r < 40) kj_client_request(c, KJ_OP_CHALLENGE, opp[rnd() % (uint32_t)n].no, now_ms);
        break;
    }
    case KJ_ST_CHALLENGED: kj_client_request(c, r < 85 ? KJ_OP_ACCEPT : KJ_OP_DECLINE, 0, now_ms); break;
    case KJ_ST_MATCHED: if (r < 3) kj_client_request(c, KJ_OP_CANCEL, 0, now_ms); break;
    case KJ_ST_DUEL:
        if (v->my_lock == KJ_CARD_NONE) {
            uint8_t pick[3];
            int n = 0;
            for (uint8_t k = 0; k < KJ_CARD_TYPES; k++) {
                if (v->cards[k]) pick[n++] = k;
            }
            if (n) kj_client_request(c, KJ_OP_PLAY, pick[rnd() % (uint32_t)n], now_ms);
        }
        break;
    default: break;
    }
}

static int sim_index_of_no(uint8_t no)
{
    const kj_player_t *p = kj_rules_player_by_no(&server.game, no);
    return p && !p->is_bot ? node_of_mac(p->mac) : -1;
}

static void step(void)
{
    schedule_bumps();
    kj_outbox_t o;
    kj_outbox_clear(&o);
    kj_server_tick(&server, now_ms, &o);
    send_from(HOST_NODE, &o);
    for (int i = 0; i < HUMANS; i++) {
        act(i);
        kj_outbox_clear(&o);
        kj_client_tick(&players[i].c, now_ms, &o);
        send_from(i, &o);
        kj_client_take_view_changed(&players[i].c);
        kj_client_take_req_failed(&players[i].c);
        kj_client_take_kicked(&players[i].c, NULL);
    }
    while (heap_n > 0 && (int32_t)(heap[0].at - now_ms) <= 0) {
        pkt_t p = heap_pop();
        deliver(&p);
    }
    // 同一次庄家开机（epoch）内，选手接受的视图版本不倒退
    for (int i = 0; i < HUMANS; i++) {
        sim_player_t *sp = &players[i];
        const kj_client_t *c = &sp->c;
        if (!c->have_view) {
            sp->seen_any = false;
            continue;
        }
        if (sp->seen_any && c->view.epoch == sp->seen_epoch && kj_seq_diff(c->view.view_ver, sp->seen_ver) < 0) {
            fprintf(stderr, "player %d view went back %u -> %u\n", i, sp->seen_ver, c->view.view_ver);
            kj_test_failures++;
        }
        sp->seen_any = true;
        sp->seen_epoch = c->view.epoch;
        sp->seen_ver = c->view.view_ver;
    }
    kj_event_t e;
    while (kj_rules_pop_event(&server.game, &e)) {
        results_seen += e.kind == KJ_EV_RESULT;
        if (e.kind == KJ_EV_MATCH) {
            matches_seen++;
            int a = sim_index_of_no(e.a), b = sim_index_of_no(e.b);
            if (a >= 0 && b >= 0 && players[a].bump_group == players[b].bump_group) {
                groups[players[a].bump_group].matched = true;
            }
        }
    }
    now_ms += STEP_MS;
}

static void check_invariants(void)
{
    const kj_game_t *g = &server.game;
    int stars = 0, cards = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        const kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        stars += p->stars;
        cards += kj_rules_total_cards(p);
        bool paired = p->status == KJ_ST_CHALLENGING || p->status == KJ_ST_CHALLENGED || p->status == KJ_ST_DUEL ||
                      p->status == KJ_ST_MATCHED;
        if (paired) {
            CHECK(p->peer < KJ_MAX_PLAYERS);
            if (p->peer < KJ_MAX_PLAYERS) {
                const kj_player_t *q = &g->players[p->peer];
                CHECK_EQ(q->peer, i);
                if (p->status == KJ_ST_CHALLENGING) CHECK_EQ(q->status, KJ_ST_CHALLENGED);
                if (p->status == KJ_ST_DUEL || p->status == KJ_ST_MATCHED) CHECK_EQ(q->status, p->status);
            }
        }
        if (p->status == KJ_ST_MATCHED) CHECK((uint32_t)(now_ms - p->since_ms) <= KJ_MATCH_COUNTDOWN_MS + STEP_MS);
        if (p->status == KJ_ST_BUMPING) {
            CHECK((uint32_t)(now_ms - p->since_ms) <= KJ_BUMP_MAX_AGE_MS + KJ_BUMP_SETTLE_MS);
        }
        if (p->is_bot) CHECK(p->status != KJ_ST_BUMPING && p->status != KJ_ST_MATCHED);
    }
    if (g->phase != KJ_PHASE_LOBBY) {
        CHECK_EQ(stars, dealt_players * KJ_START_STARS);
        CHECK_EQ(cards, dealt_players * 12 - 2 * (int)results_seen);
    }
}

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        step();
        if ((now_ms / STEP_MS) % 50 == 0) check_invariants();
    }
}

static void check_converged(const char *when)
{
    for (int i = 0; i < HUMANS; i++) {
        kj_client_t *c = &players[i].c;
        CHECK_EQ(c->link, KJ_LINK_JOINED);
        int idx = kj_rules_find_mac(&server.game, players[i].mac);
        CHECK(idx >= 0);
        if (idx < 0) continue;
        kj_view_t want;
        kj_rules_view(&server.game, idx, now_ms, &want);
        if (c->view.view_ver != want.view_ver || c->view.status != want.status || c->view.stars != want.stars ||
            memcmp(c->view.cards, want.cards, 3) != 0 || c->view.epoch != server.epoch) {
            fprintf(stderr, "%s: player %d ver %u/%u status %u/%u\n", when, i, c->view.view_ver, want.view_ver,
                    c->view.status, want.status);
            kj_test_failures++;
        }
        CHECK(!c->pending);
    }
}

static int run_sim(uint32_t seed)
{
    int failures_before = kj_test_failures;
    rng = seed;
    now_ms = 1 + seed % 1000;
    loss_pct = 10;
    dup_pct = 1;
    actions_enabled = true;
    heap_n = 0;
    results_seen = matches_seen = delivered = dropped = overflow = 0;
    group_count = 0;
    memset(hub_knows, 0, sizeof(hub_knows));
    kj_server_init(&server, ROOM_ID, seed * 7 + 1);
    for (int i = 0; i < HUMANS; i++) {
        uint8_t mac[6] = { 0x24, 0x6F, 0x28, 0x20, 0x00, (uint8_t)(i + 1) };
        memset(&players[i], 0, sizeof(players[i]));
        memcpy(players[i].mac, mac, 6);
        kj_client_init(&players[i].c, seed * 131u + 1000u + (uint32_t)i);
    }
    for (int b = 0; b < BOTS; b++) kj_server_command(&server, KJ_CMD_BOT_ADD, 0, now_ms);

    // 入座：真实设备开机后每秒发 DISCOVER，hub 早就记住了它们的地址（信标扇出依赖这个）；这里直接预置。
    for (int i = 0; i < HUMANS; i++) hub_knows[i] = true;
    run_ms(8000);
    CHECK_EQ(kj_rules_player_count(&server.game), HUMANS + BOTS);
    dealt_players = HUMANS + BOTS;
    CHECK_EQ(kj_server_command(&server, KJ_CMD_START, 0, now_ms), KJ_N_NONE);
    run_ms(25000);
    CHECK(results_seen > 5);

    // 选手 3 重启（新的开机号）：重新入座拿回原座位
    int idx3 = kj_rules_find_mac(&server.game, players[3].mac);
    kj_client_init(&players[3].c, seed ^ 0xBEEFu);
    players[3].seen_any = false;
    run_ms(6000);
    CHECK_EQ(players[3].c.link, KJ_LINK_JOINED);
    CHECK_EQ(players[3].c.view.no, kj_no_of(idx3));

    // hub 重启：路由表清空，靠选手心跳与庄家信标重新学到地址
    memset(hub_knows, 0, sizeof(hub_knows));
    heap_n = 0;
    run_ms(8000);

    // 庄家重启：保存 → 新的开机（epoch 变化）→ 恢复
    uint8_t blob[KJ_PERSIST_MAX];
    size_t n = kj_persist_save(&server.game, blob, sizeof(blob));
    CHECK(n > 0);
    uint16_t old_epoch = server.epoch;
    kj_server_init(&server, ROOM_ID, seed ^ 0x5A5Au);
    CHECK(server.epoch != old_epoch);
    CHECK(kj_persist_load(&server.game, blob, n, now_ms));
    {
        int cards = 0;
        for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
            if (server.game.players[i].used) cards += kj_rules_total_cards(&server.game.players[i]);
        }
        results_seen = (dealt_players * 12 - cards) / 2;
    }
    long before = results_seen;
    run_ms(60000);
    check_invariants();
    CHECK(results_seen > before);

    actions_enabled = false;
    loss_pct = dup_pct = 0;
    run_ms(10000);
    check_converged("after quiet period");
    CHECK_EQ(kj_server_command(&server, KJ_CMD_END, 0, now_ms), KJ_N_NONE);
    loss_pct = 10;
    run_ms(6000);
    loss_pct = 0;
    run_ms(4000);
    check_converged("after end");
    check_invariants();

    int clean = 0, clean_ok = 0;
    for (int k = 0; k < group_count; k++) {
        if (groups[k].size == 2 && groups[k].sent == 2) {
            clean++;
            clean_ok += groups[k].matched;
        }
    }
    printf("sim_hub seed %u: %ld delivered, %ld dropped, %ld duels, %ld matches, clean bump pairs %d/%d\n",
           (unsigned)seed, delivered, dropped, results_seen, matches_seen, clean_ok, clean);
    CHECK_EQ(overflow, 0);
    CHECK(clean > 0);
    CHECK(clean_ok * 10 >= clean * 8);   // 每跳 10% 丢包、乱序下，两人都按下的碰拳至少八成配对成功
    return kj_test_failures - failures_before;
}

int main(void)
{
    static const uint32_t seeds[] = { 0x2468ACE1u, 0x13579BDFu, 20261005u };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        if (run_sim(seeds[i])) fprintf(stderr, "seed %u failed\n", (unsigned)seeds[i]);
    }
    KJ_TEST_DONE("test_kj_sim_hub");
}
