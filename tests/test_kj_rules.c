// tests/test_kj_rules.c —— 限定猜拳规则引擎：发牌、对决、星星转移、过关 / 出局 / 失败、超时与庄家命令。
#include "kj_rules.h"
#include "kj_test.h"

#include <string.h>

static kj_game_t g;

static void mac_of(int n, uint8_t mac[6])
{
    uint8_t m[6] = { 0x24, 0x6F, 0x28, 0x00, (uint8_t)(n >> 8), (uint8_t)n };
    memcpy(mac, m, 6);
}

static int join(int n, uint32_t now)
{
    uint8_t mac[6];
    mac_of(n, mac);
    return kj_rules_join(&g, mac, now);
}

static void keep_online(uint32_t now)
{
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) {
        if (g.players[i].used) kj_rules_seen(&g, i, -50, now);
    }
}

static int total_stars(void)
{
    int s = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) s += g.players[i].used ? g.players[i].stars : 0;
    return s;
}

// 让 a 挑战 b、b 应战，然后双方出牌。
static void duel(int a, int b, uint8_t ca, uint8_t cb, uint32_t now)
{
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(b), now), KJ_N_NONE);
    CHECK_EQ(kj_rules_respond(&g, b, true, now), KJ_N_NONE);
    CHECK_EQ(kj_rules_play(&g, a, ca, now), KJ_N_NONE);
    CHECK_EQ(kj_rules_play(&g, b, cb, now), KJ_N_NONE);
}

static void test_compare(void)
{
    CHECK_EQ(kj_rules_compare(KJ_ROCK, KJ_SCISSORS), 0);
    CHECK_EQ(kj_rules_compare(KJ_SCISSORS, KJ_PAPER), 0);
    CHECK_EQ(kj_rules_compare(KJ_PAPER, KJ_ROCK), 0);
    CHECK_EQ(kj_rules_compare(KJ_SCISSORS, KJ_ROCK), 1);
    CHECK_EQ(kj_rules_compare(KJ_PAPER, KJ_SCISSORS), 1);
    CHECK_EQ(kj_rules_compare(KJ_ROCK, KJ_PAPER), 1);
    for (uint8_t c = 0; c < KJ_CARD_TYPES; c++) CHECK_EQ(kj_rules_compare(c, c), -1);
}

static void test_join_and_start(void)
{
    kj_rules_init(&g);
    CHECK_EQ(kj_rules_start(&g, 0), KJ_N_INVALID);           // 没人
    int a = join(1, 10);
    CHECK_EQ(a, 0);
    CHECK_EQ(g.players[a].status, KJ_ST_WAITING);
    CHECK_EQ(kj_rules_start(&g, 20), KJ_N_INVALID);          // 1 人不能开局
    CHECK_EQ(join(1, 30), a);                                // 同一 MAC 重连得到同一座位
    int b = join(2, 40);
    CHECK_EQ(b, 1);
    CHECK_EQ(kj_rules_player_count(&g), 2);
    CHECK_EQ(kj_rules_start(&g, 50), KJ_N_NONE);
    CHECK_EQ(g.phase, KJ_PHASE_RUNNING);
    for (int i = 0; i < 2; i++) {
        CHECK_EQ(g.players[i].status, KJ_ST_IDLE);
        CHECK_EQ(g.players[i].stars, KJ_START_STARS);
        for (int c = 0; c < KJ_CARD_TYPES; c++) CHECK_EQ(g.players[i].cards[c], KJ_CARDS_PER_TYPE);
    }
    CHECK_EQ(kj_rules_start(&g, 60), KJ_N_INVALID);          // 不能重复开局
    // 进行中入座：直接发牌
    int c = join(3, 70);
    CHECK_EQ(g.players[c].status, KJ_ST_IDLE);
    CHECK_EQ(kj_rules_total_cards(&g.players[c]), 12);
}

static void test_duel_win_lose_draw(void)
{
    kj_rules_init(&g);
    int a = join(1, 0), b = join(2, 0);
    kj_rules_start(&g, 0);
    uint16_t va = g.players[a].view_ver;

    duel(a, b, KJ_ROCK, KJ_SCISSORS, 100);
    CHECK_EQ(g.players[a].stars, 4);
    CHECK_EQ(g.players[b].stars, 2);
    CHECK_EQ(g.players[a].cards[KJ_ROCK], 3);
    CHECK_EQ(g.players[b].cards[KJ_SCISSORS], 3);
    CHECK_EQ(g.players[a].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[b].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[a].res_outcome, KJ_OUT_WIN);
    CHECK_EQ(g.players[b].res_outcome, KJ_OUT_LOSE);
    CHECK_EQ(g.players[a].res_opp, KJ_SCISSORS);
    CHECK_EQ(g.players[b].res_opp, KJ_ROCK);
    CHECK_EQ(g.players[b].res_opp_no, kj_no_of(a));
    CHECK_EQ(g.players[a].wins, 1);
    CHECK_EQ(g.players[b].losses, 1);
    CHECK(g.players[a].view_ver != va);
    uint16_t first = g.players[a].res_duel_id;
    CHECK(first != 0);

    duel(b, a, KJ_PAPER, KJ_PAPER, 200);   // 平局：消耗牌，星星不动
    CHECK_EQ(g.players[a].stars, 4);
    CHECK_EQ(g.players[b].stars, 2);
    CHECK_EQ(g.players[a].cards[KJ_PAPER], 3);
    CHECK_EQ(g.players[b].cards[KJ_PAPER], 3);
    CHECK_EQ(g.players[a].res_outcome, KJ_OUT_DRAW);
    CHECK_EQ(g.players[a].draws, 1);
    CHECK(g.players[a].res_duel_id != first);
    CHECK_EQ(total_stars(), 6);

    // 应战方赢
    duel(a, b, KJ_ROCK, KJ_PAPER, 300);
    CHECK_EQ(g.players[a].stars, 3);
    CHECK_EQ(g.players[b].stars, 3);
}

static void test_view_hides_opponent_card(void)
{
    kj_rules_init(&g);
    int a = join(1, 0), b = join(2, 0);
    kj_rules_start(&g, 0);
    kj_rules_challenge(&g, a, kj_no_of(b), 10);
    kj_view_t v;
    kj_rules_view(&g, b, 10, &v);
    CHECK_EQ(v.status, KJ_ST_CHALLENGED);
    CHECK_EQ(v.peer_no, kj_no_of(a));
    CHECK_EQ(v.deadline_s, KJ_CHALLENGE_TIMEOUT_MS / 1000);
    kj_rules_respond(&g, b, true, 20);
    kj_rules_play(&g, a, KJ_PAPER, 30);
    kj_rules_view(&g, b, 30, &v);
    CHECK_EQ(v.status, KJ_ST_DUEL);
    CHECK_EQ(v.peer_locked, 1);
    CHECK_EQ(v.my_lock, KJ_CARD_NONE);
    // 视图里没有任何字段带出对手的暗牌
    CHECK(v.res_opp == KJ_CARD_NONE);
    kj_rules_view(&g, a, 30, &v);
    CHECK_EQ(v.my_lock, KJ_PAPER);
    CHECK_EQ(v.peer_locked, 0);
    // 已出牌不能反悔 / 放弃
    CHECK_EQ(kj_rules_play(&g, a, KJ_ROCK, 31), KJ_N_INVALID);
    CHECK_EQ(kj_rules_withdraw(&g, a, 31), KJ_N_INVALID);
    // 未出牌的一方可以放弃：对决作废，暗牌不扣
    CHECK_EQ(kj_rules_withdraw(&g, b, 32), KJ_N_NONE);
    CHECK_EQ(g.players[a].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[a].cards[KJ_PAPER], 4);
    CHECK_EQ(g.players[a].notice, KJ_N_WITHDRAWN);
}

static void test_invalid_operations(void)
{
    kj_rules_init(&g);
    int a = join(1, 0), b = join(2, 0), c = join(3, 0);
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(b), 0), KJ_N_NOT_RUNNING);
    kj_rules_start(&g, 0);
    keep_online(0);
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(a), 0), KJ_N_BUSY);    // 不能挑战自己
    CHECK_EQ(kj_rules_challenge(&g, a, 99, 0), KJ_N_BUSY);             // 空座位
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(b), 0), KJ_N_NONE);
    CHECK_EQ(kj_rules_challenge(&g, c, kj_no_of(b), 0), KJ_N_BUSY);    // b 已被挑战
    CHECK_EQ(kj_rules_challenge(&g, c, kj_no_of(a), 0), KJ_N_BUSY);    // a 正在等应战
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(c), 0), KJ_N_INVALID); // 一次只能挑战一人
    CHECK_EQ(kj_rules_respond(&g, a, true, 0), KJ_N_INVALID);          // 挑战者不能应战
    CHECK_EQ(kj_rules_play(&g, a, KJ_ROCK, 0), KJ_N_INVALID);          // 还没开始对决
    CHECK_EQ(kj_rules_cancel(&g, b, 0), KJ_N_INVALID);
    CHECK_EQ(kj_rules_respond(&g, b, false, 0), KJ_N_NONE);            // 拒绝
    CHECK_EQ(g.players[a].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[a].notice, KJ_N_DECLINED);
    // 撤回挑战
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(c), 1), KJ_N_NONE);
    CHECK_EQ(kj_rules_cancel(&g, a, 2), KJ_N_NONE);
    CHECK_EQ(g.players[c].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[c].notice, KJ_N_CANCELLED);
    // 没有的牌不能出
    g.players[a].cards[KJ_ROCK] = 0;
    duel(a, b, KJ_PAPER, KJ_PAPER, 3);
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(b), 4), KJ_N_NONE);
    CHECK_EQ(kj_rules_respond(&g, b, true, 4), KJ_N_NONE);
    CHECK_EQ(kj_rules_play(&g, a, KJ_ROCK, 5), KJ_N_NO_CARD);
    CHECK_EQ(kj_rules_play(&g, a, 7, 5), KJ_N_INVALID);
}

static void test_elimination_and_clear(void)
{
    kj_rules_init(&g);
    int a = join(1, 0), b = join(2, 0);
    kj_rules_start(&g, 0);
    // a 连赢三次：b 星星归零出局
    duel(a, b, KJ_ROCK, KJ_SCISSORS, 1);
    duel(a, b, KJ_SCISSORS, KJ_PAPER, 2);
    duel(a, b, KJ_PAPER, KJ_ROCK, 3);
    CHECK_EQ(g.players[b].stars, 0);
    CHECK_EQ(g.players[b].status, KJ_ST_ELIMINATED);
    CHECK_EQ(g.players[a].stars, 6);
    CHECK_EQ(g.players[a].status, KJ_ST_IDLE);   // 还有 9 张牌，未过关
    CHECK(!kj_rules_available(&g, b, 3));
    CHECK_EQ(kj_rules_challenge(&g, a, kj_no_of(b), 4), KJ_N_BUSY);
    CHECK_EQ(total_stars(), 6);

    kj_event_t e;
    int results = 0, elims = 0;
    while (kj_rules_pop_event(&g, &e)) {
        results += e.kind == KJ_EV_RESULT;
        elims += e.kind == KJ_EV_ELIMINATED;
        if (e.kind == KJ_EV_RESULT) CHECK_EQ(e.winner, kj_no_of(a));
    }
    CHECK_EQ(results, 3);
    CHECK_EQ(elims, 1);

    // 过关：手牌出完且星星 ≥ 3。用两名新选手逐张平局打完 12 张。
    kj_rules_init(&g);
    a = join(1, 0);
    b = join(2, 0);
    kj_rules_start(&g, 0);
    for (int k = 0; k < 12; k++) duel(a, b, (uint8_t)(k % 3), (uint8_t)(k % 3), (uint32_t)(10 + k));
    CHECK_EQ(g.players[a].status, KJ_ST_CLEARED);
    CHECK_EQ(g.players[b].status, KJ_ST_CLEARED);
    CHECK_EQ(g.players[a].draws, 12);

    // 失败：手牌出完但星星不足 3。
    kj_rules_init(&g);
    a = join(1, 0);
    b = join(2, 0);
    kj_rules_start(&g, 0);
    duel(a, b, KJ_ROCK, KJ_SCISSORS, 1);   // a 4★ b 2★
    for (int k = 0; k < 11; k++) {
        // 剩下的 11 张逐张平局
        uint8_t card = KJ_CARD_NONE;
        for (uint8_t c = 0; c < KJ_CARD_TYPES; c++) {
            if (g.players[a].cards[c] && g.players[b].cards[c]) card = c;
        }
        if (card == KJ_CARD_NONE) break;
        duel(a, b, card, card, (uint32_t)(20 + k));
    }
    // a 出了 1 张石头 + 平局，b 出了 1 张剪刀：手牌组合不同，最后可能剩一张无法平局的牌。
    if (kj_rules_total_cards(&g.players[a]) == 1) {
        uint8_t ca = 0, cb = 0;
        for (uint8_t c = 0; c < KJ_CARD_TYPES; c++) {
            if (g.players[a].cards[c]) ca = c;
            if (g.players[b].cards[c]) cb = c;
        }
        duel(a, b, ca, cb, 40);
    }
    CHECK(kj_rules_total_cards(&g.players[b]) == 0);
    if (g.players[b].stars > 0 && g.players[b].stars < KJ_CLEAR_STARS) {
        CHECK_EQ(g.players[b].status, KJ_ST_FAILED);
        CHECK_EQ(g.players[b].final_reason, KJ_FINAL_NO_CARDS);
    }
    CHECK_EQ(total_stars(), 6);
}

static void test_timeouts_and_offline(void)
{
    kj_rules_init(&g);
    int a = join(1, 0), b = join(2, 0);
    kj_rules_start(&g, 0);
    kj_rules_challenge(&g, a, kj_no_of(b), 1000);
    keep_online(1000 + KJ_CHALLENGE_TIMEOUT_MS - 1000);
    kj_rules_tick(&g, 1000 + KJ_CHALLENGE_TIMEOUT_MS - 1);
    CHECK_EQ(g.players[b].status, KJ_ST_CHALLENGED);
    kj_rules_tick(&g, 1000 + KJ_CHALLENGE_TIMEOUT_MS);
    CHECK_EQ(g.players[a].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[b].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[a].notice, KJ_N_TIMEOUT);
    CHECK_EQ(g.players[b].notice, KJ_N_TIMEOUT);

    // 对决中 b 断线：超过阈值后作废，a 收到通知，暗牌退回
    uint32_t t = 30000;
    keep_online(t);
    kj_rules_challenge(&g, a, kj_no_of(b), t);
    kj_rules_respond(&g, b, true, t);
    kj_rules_play(&g, a, KJ_ROCK, t);
    for (uint32_t now = t; now <= t + KJ_DUEL_OFFLINE_ABORT_MS + 500; now += 500) {
        kj_rules_seen(&g, a, -40, now);   // 只有 a 在发心跳
        kj_rules_tick(&g, now);
    }
    CHECK_EQ(g.players[a].status, KJ_ST_IDLE);
    CHECK_EQ(g.players[a].notice, KJ_N_ABORTED);
    CHECK_EQ(g.players[a].cards[KJ_ROCK], 4);
    CHECK(!kj_rules_online(&g, b, t + KJ_DUEL_OFFLINE_ABORT_MS + 500));
    // 离线的人不在空闲名单里
    CHECK(!kj_rules_available(&g, b, t + KJ_DUEL_OFFLINE_ABORT_MS + 500));
}

static void test_end_new_reset_bots(void)
{
    kj_rules_init(&g);
    int a = join(1, 0), b = join(2, 0), c = join(3, 0);
    int bot = kj_rules_add_bot(&g, 0);
    CHECK(bot >= 0);
    CHECK(g.players[bot].is_bot);
    CHECK(kj_rules_online(&g, bot, 999999));
    kj_rules_start(&g, 0);
    keep_online(10);
    // a 打完 12 张平局过关
    for (int k = 0; k < 12; k++) duel(a, b, (uint8_t)(k % 3), (uint8_t)(k % 3), (uint32_t)(10 + k));
    CHECK_EQ(g.players[a].status, KJ_ST_CLEARED);
    // c 正在挑战 bot 时宣布结束：挑战撤销，c 与 bot 有手牌 → 时间到失败
    kj_rules_challenge(&g, c, kj_no_of(bot), 30);
    CHECK_EQ(kj_rules_end(&g, 40), KJ_N_NONE);
    CHECK_EQ(g.phase, KJ_PHASE_ENDED);
    CHECK_EQ(g.players[a].status, KJ_ST_CLEARED);
    CHECK_EQ(g.players[c].status, KJ_ST_FAILED);
    CHECK_EQ(g.players[c].final_reason, KJ_FINAL_TIME_UP);
    CHECK_EQ(g.players[bot].status, KJ_ST_FAILED);
    CHECK_EQ(kj_rules_end(&g, 41), KJ_N_INVALID);

    kj_summary_t s;
    kj_rules_summary(&g, 50, &s);
    CHECK_EQ(s.seated, 4);
    CHECK_EQ(s.bots, 1);
    CHECK_EQ(s.cleared, 2);   // a、b 都打完 12 张平局
    CHECK_EQ(s.failed, 2);

    uint16_t gid = g.game_id;
    kj_rules_new_game(&g, 60);
    CHECK_EQ(g.phase, KJ_PHASE_LOBBY);
    CHECK_EQ(g.game_id, gid + 1);
    CHECK_EQ(kj_rules_player_count(&g), 4);
    CHECK_EQ(g.players[a].status, KJ_ST_WAITING);
    CHECK_EQ(kj_rules_total_cards(&g.players[a]), 0);
    CHECK_EQ(kj_rules_remove_bot(&g, 61), KJ_N_NONE);
    CHECK_EQ(kj_rules_player_count(&g), 3);
    CHECK_EQ(kj_rules_remove_bot(&g, 62), KJ_N_BUSY);   // 没有电脑选手了

    // 移除选手会释放座位；视图版本不回退
    uint16_t ver = g.players[c].view_ver;
    CHECK_EQ(kj_rules_remove(&g, c, 70), KJ_N_NONE);
    CHECK(!g.players[c].used);
    CHECK(g.players[c].view_ver != ver);
    CHECK(kj_rules_take_dirty(&g, c));

    kj_rules_reset(&g, 80);
    CHECK_EQ(kj_rules_player_count(&g), 0);
    CHECK_EQ(g.phase, KJ_PHASE_LOBBY);
    CHECK_EQ(g.game_id, gid + 2);
}

static void test_capacity_and_events(void)
{
    kj_rules_init(&g);
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) CHECK_EQ(join(i + 1, 0), i);
    CHECK_EQ(join(KJ_MAX_PLAYERS + 1, 0), -1);
    CHECK_EQ(kj_rules_add_bot(&g, 0), -1);
    // 事件环满了丢最旧的，不越界
    kj_event_t e;
    int n = 0;
    while (kj_rules_pop_event(&g, &e)) n++;
    CHECK_EQ(n, KJ_EVENT_RING);
    // 128 人同时开局，星星守恒
    CHECK_EQ(kj_rules_start(&g, 1), KJ_N_NONE);
    CHECK_EQ(total_stars(), KJ_MAX_PLAYERS * KJ_START_STARS);
    keep_online(2);
    for (int i = 0; i + 1 < KJ_MAX_PLAYERS; i += 2) duel(i, i + 1, KJ_ROCK, KJ_SCISSORS, 3);
    CHECK_EQ(total_stars(), KJ_MAX_PLAYERS * KJ_START_STARS);
    kj_summary_t s;
    kj_rules_summary(&g, 3, &s);
    CHECK_EQ(s.cards[KJ_ROCK] + s.cards[KJ_SCISSORS] + s.cards[KJ_PAPER], KJ_MAX_PLAYERS * 12 - KJ_MAX_PLAYERS);
}

int main(void)
{
    test_compare();
    test_join_and_start();
    test_duel_win_lose_draw();
    test_view_hides_opponent_card();
    test_invalid_operations();
    test_elimination_and_clear();
    test_timeouts_and_offline();
    test_end_new_reset_bots();
    test_capacity_and_events();
    KJ_TEST_DONE("test_kj_rules");
}
