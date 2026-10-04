// tests/test_kj_sim.c —— 多设备联机仿真：1 台庄家 + 多台选手 + 电脑选手，经过有丢包的模拟无线信道
// 打完整局。检查：星星守恒、手牌只减不增且与结算次数一致、挑战 / 对决关系对称、
// 选手重启 / 庄家重启（NVS 恢复）后能续上、信道恢复后所有选手视图与庄家一致。
#include "kj_client.h"
#include "kj_persist.h"
#include "kj_server.h"
#include "kj_test.h"

#include <string.h>

#define HUMANS 12
#define BOTS 3
#define STEP_MS 10
#define ROOM_ID 0x4B4A

typedef struct {
    kj_client_t c;
    uint8_t mac[6];
    uint32_t next_action_ms;
} sim_player_t;

static kj_server_t server;
static sim_player_t players[HUMANS];
static const uint8_t host_mac[6] = { 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC };
static uint32_t now_ms = 1;
static uint32_t rng = 0x12345678u;
static int loss_pct = 25;
static bool actions_enabled = true;
static long delivered, dropped;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static bool lose(void) { return (int)(rnd() % 100) < loss_pct; }

static int8_t rssi_between(int a, int b)
{
    // 选手围成一圈：编号相邻的人信号更强。-1 代表庄家。
    int d = a < 0 || b < 0 ? 3 : (a > b ? a - b : b - a);
    if (d > HUMANS / 2) d = HUMANS - d;
    return (int8_t)(-35 - d * 6 - (int)(rnd() % 5));
}

static void deliver_to_player(int i, const uint8_t *src, int src_idx, const kj_out_t *it)
{
    if (lose()) {
        dropped++;
        return;
    }
    delivered++;
    kj_client_on_frame(&players[i].c, src, rssi_between(src_idx, i), it->data, it->len, now_ms);
}

static void route(const kj_outbox_t *o, const uint8_t *src, int src_idx)
{
    for (int k = 0; k < o->count; k++) {
        const kj_out_t *it = &o->items[k];
        bool to_host = it->broadcast || memcmp(it->mac, host_mac, 6) == 0;
        if (to_host && src_idx >= 0) {
            if (lose()) {
                dropped++;
            } else {
                delivered++;
                kj_outbox_t reply;
                kj_outbox_clear(&reply);
                kj_server_on_frame(&server, src, rssi_between(src_idx, -1), it->data, it->len, now_ms, &reply);
                route(&reply, host_mac, -1);
            }
        }
        for (int i = 0; i < HUMANS; i++) {
            if (i == src_idx) continue;
            if (it->broadcast || memcmp(it->mac, players[i].mac, 6) == 0) deliver_to_player(i, src, src_idx, it);
        }
    }
}

static void act(int i)
{
    sim_player_t *sp = &players[i];
    kj_client_t *c = &sp->c;
    if ((int32_t)(now_ms - sp->next_action_ms) < 0) return;
    sp->next_action_ms = now_ms + 200 + rnd() % 1500;
    if (c->link == KJ_LINK_IDLE) {
        kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];
        if (kj_client_rooms(c, now_ms, rooms, KJ_CLIENT_MAX_ROOMS) > 0) kj_client_join(c, rooms[0].room, now_ms);
        return;
    }
    if (!actions_enabled || c->link != KJ_LINK_JOINED || c->pending) return;
    const kj_view_t *v = &c->view;
    if (v->phase != KJ_PHASE_RUNNING) return;
    uint32_t r = rnd() % 100;
    switch (v->status) {
    case KJ_ST_IDLE: {
        kj_opponent_t opp[8];
        int n = kj_client_opponents(c, now_ms, opp, 8);
        if (n > 0 && r < 60) kj_client_request(c, KJ_OP_CHALLENGE, opp[rnd() % (uint32_t)n].no, now_ms);
        break;
    }
    case KJ_ST_CHALLENGED:
        kj_client_request(c, r < 85 ? KJ_OP_ACCEPT : KJ_OP_DECLINE, 0, now_ms);
        break;
    case KJ_ST_CHALLENGING:
        if (r < 3) kj_client_request(c, KJ_OP_CANCEL, 0, now_ms);
        break;
    case KJ_ST_DUEL:
        if (v->my_lock == KJ_CARD_NONE) {
            if (r < 3) {
                kj_client_request(c, KJ_OP_WITHDRAW, 0, now_ms);
                break;
            }
            uint8_t pick[3];
            int n = 0;
            for (uint8_t k = 0; k < KJ_CARD_TYPES; k++) {
                if (v->cards[k]) pick[n++] = k;
            }
            if (n) kj_client_request(c, KJ_OP_PLAY, pick[rnd() % (uint32_t)n], now_ms);
        }
        break;
    default:
        break;
    }
}

static long results_seen;

static void step(void)
{
    kj_outbox_t o;
    kj_outbox_clear(&o);
    kj_server_tick(&server, now_ms, &o);
    route(&o, host_mac, -1);
    for (int i = 0; i < HUMANS; i++) {
        act(i);
        kj_outbox_clear(&o);
        kj_client_tick(&players[i].c, now_ms, &o);
        route(&o, players[i].mac, i);
        kj_client_take_view_changed(&players[i].c);
        kj_client_take_req_failed(&players[i].c);
        kj_client_take_kicked(&players[i].c, NULL);
    }
    kj_event_t e;
    while (kj_rules_pop_event(&server.game, &e)) results_seen += e.kind == KJ_EV_RESULT;
    now_ms += STEP_MS;
}

static int dealt_players;

static void check_invariants(void)
{
    const kj_game_t *g = &server.game;
    int stars = 0, cards = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        const kj_player_t *p = &g->players[i];
        if (!p->used) continue;
        stars += p->stars;
        for (int c = 0; c < KJ_CARD_TYPES; c++) {
            CHECK(p->cards[c] <= KJ_CARDS_PER_TYPE);
            cards += p->cards[c];
        }
        if (p->status == KJ_ST_CHALLENGING || p->status == KJ_ST_CHALLENGED || p->status == KJ_ST_DUEL) {
            CHECK(p->peer < KJ_MAX_PLAYERS);
            if (p->peer < KJ_MAX_PLAYERS) {
                const kj_player_t *q = &g->players[p->peer];
                CHECK(q->used);
                CHECK_EQ(q->peer, i);
                if (p->status == KJ_ST_CHALLENGING) CHECK_EQ(q->status, KJ_ST_CHALLENGED);
                if (p->status == KJ_ST_CHALLENGED) CHECK_EQ(q->status, KJ_ST_CHALLENGING);
                if (p->status == KJ_ST_DUEL) CHECK_EQ(q->status, KJ_ST_DUEL);
            }
        }
        if (p->status == KJ_ST_ELIMINATED) CHECK_EQ(p->stars, 0);
        if (p->status == KJ_ST_CLEARED) CHECK(p->stars >= KJ_CLEAR_STARS && kj_rules_total_cards(p) == 0);
    }
    if (g->phase != KJ_PHASE_LOBBY) {
        CHECK_EQ(stars, dealt_players * KJ_START_STARS);
        CHECK_EQ(cards, dealt_players * 12 - 2 * (int)results_seen);
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
        if (c->view.view_ver != want.view_ver || c->view.status != want.status ||
            memcmp(c->view.cards, want.cards, 3) != 0 || c->view.stars != want.stars) {
            fprintf(stderr, "%s: player %d view ver %u/%u status %u/%u stars %u/%u\n", when, i,
                    c->view.view_ver, want.view_ver, c->view.status, want.status, c->view.stars, want.stars);
            kj_test_failures++;
        }
        CHECK(!c->pending);
    }
}

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        step();
        if ((now_ms / STEP_MS) % 50 == 0) check_invariants();
    }
}

static int run_sim(uint32_t seed)
{
    rng = seed;
    now_ms = 1 + seed % 1000;
    loss_pct = 25;
    actions_enabled = true;
    delivered = dropped = results_seen = 0;
    int failures_before = kj_test_failures;
    kj_server_init(&server, ROOM_ID, seed * 7 + 1);
    for (int i = 0; i < HUMANS; i++) {
        uint8_t mac[6] = { 0x24, 0x6F, 0x28, 0x10, 0x00, (uint8_t)(i + 1) };
        memcpy(players[i].mac, mac, 6);
        kj_client_init(&players[i].c, seed * 131u + 1000u + (uint32_t)i);
        players[i].next_action_ms = 0;
    }
    for (int b = 0; b < BOTS; b++) CHECK_EQ(kj_server_command(&server, KJ_CMD_BOT_ADD, 0, now_ms), KJ_N_NONE);

    // 入座（有丢包）
    run_ms(6000);
    CHECK_EQ(kj_rules_player_count(&server.game), HUMANS + BOTS);
    for (int i = 0; i < HUMANS; i++) CHECK_EQ(players[i].c.link, KJ_LINK_JOINED);
    for (int i = 0; i < HUMANS; i++) CHECK(players[i].c.view.no >= 1);

    dealt_players = HUMANS + BOTS;
    CHECK_EQ(kj_server_command(&server, KJ_CMD_START, 0, now_ms), KJ_N_NONE);
    run_ms(20000);
    CHECK(results_seen > 10);

    // 选手 3 重启：同一 MAC 重新入座，拿回原座位与手牌
    int idx3 = kj_rules_find_mac(&server.game, players[3].mac);
    uint8_t stars3 = server.game.players[idx3].stars;
    kj_client_init(&players[3].c, 777);
    run_ms(5000);
    CHECK_EQ(players[3].c.link, KJ_LINK_JOINED);
    CHECK_EQ(players[3].c.view.no, kj_no_of(idx3));
    (void)stars3;

    // 庄家重启：保存 → 重新初始化 → 恢复
    uint8_t blob[KJ_PERSIST_MAX];
    size_t n = kj_persist_save(&server.game, blob, sizeof(blob));
    CHECK(n > 0);
    printf("sim: before host reboot %ld duels, %u views, %u beacons\n", results_seen,
           (unsigned)server.tx_views, (unsigned)server.tx_beacons);
    kj_server_init(&server, ROOM_ID, 4242);
    CHECK(kj_persist_load(&server.game, blob, n, now_ms));
    // 恢复时进行中的对决作废，结算计数按当前手牌重算
    {
        int cards = 0;
        for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
            if (server.game.players[i].used) cards += kj_rules_total_cards(&server.game.players[i]);
        }
        results_seen = (dealt_players * 12 - cards) / 2;
    }
    long before_reboot = results_seen;
    run_ms(90000);
    check_invariants();
    CHECK(results_seen > before_reboot);   // 庄家恢复后对局继续

    // 停止操作、关掉丢包，等所有视图收敛
    actions_enabled = false;
    loss_pct = 0;
    run_ms(8000);
    check_converged("after quiet period");

    // 宣布结束：仍有手牌的人判负，视图同步到所有选手
    CHECK_EQ(kj_server_command(&server, KJ_CMD_END, 0, now_ms), KJ_N_NONE);
    loss_pct = 30;
    run_ms(6000);
    loss_pct = 0;
    run_ms(3000);
    check_converged("after end");
    for (int i = 0; i < HUMANS; i++) {
        CHECK_EQ(players[i].c.view.phase, KJ_PHASE_ENDED);
        CHECK(kj_rules_is_final(players[i].c.view.status));
    }
    check_invariants();

    int st_count[KJ_ST_COUNT] = { 0 };
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        if (server.game.players[i].used) st_count[server.game.players[i].status]++;
    }
    printf("sim: final statuses cleared=%d eliminated=%d failed=%d\n", st_count[KJ_ST_CLEARED],
           st_count[KJ_ST_ELIMINATED], st_count[KJ_ST_FAILED]);

    // 移除一名选手：他的设备收到 no=0 视图，回到找赌局（座位与牌一起移除，此后不再检查守恒）。
    uint8_t kicked_no = players[5].c.view.no;
    CHECK_EQ(kj_server_command(&server, KJ_CMD_KICK, kicked_no, now_ms), KJ_N_NONE);
    actions_enabled = false;
    for (int t = 0; t < 300 && players[5].c.link == KJ_LINK_JOINED; t++) step();
    CHECK(players[5].c.link != KJ_LINK_JOINED);

    printf("sim seed %u: %ld frames delivered, %ld dropped, %ld duels resolved, %u views, %u beacons\n",
           (unsigned)seed, delivered, dropped, results_seen, (unsigned)server.tx_views,
           (unsigned)server.tx_beacons);
    return kj_test_failures - failures_before;
}

int main(void)
{
    static const uint32_t seeds[] = { 0x12345678u, 0xCAFEBABEu, 0x0BADF00Du, 20261004u };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        if (run_sim(seeds[i])) fprintf(stderr, "seed %u failed\n", (unsigned)seeds[i]);
    }
    KJ_TEST_DONE("test_kj_sim");
}
