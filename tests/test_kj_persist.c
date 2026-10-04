// tests/test_kj_persist.c —— 赌局状态保存 / 恢复：往返、进行中对决的回退、损坏数据拒收。
#include "kj_persist.h"
#include "kj_test.h"

#include <string.h>

static kj_game_t g, h;

int main(void)
{
    uint8_t buf[KJ_PERSIST_MAX];
    kj_rules_init(&g);
    uint8_t m1[6] = { 1, 2, 3, 4, 5, 6 }, m2[6] = { 9, 8, 7, 6, 5, 4 }, m3[6] = { 7, 7, 7, 7, 7, 7 };
    int a = kj_rules_join(&g, m1, 0);
    int b = kj_rules_join(&g, m2, 0);
    int c = kj_rules_join(&g, m3, 0);
    int bot = kj_rules_add_bot(&g, 0);
    kj_rules_start(&g, 0);
    for (int i = 0; i < 4; i++) kj_rules_seen(&g, i, -50, 0);
    kj_rules_challenge(&g, a, kj_no_of(b), 1);
    kj_rules_respond(&g, b, true, 1);
    kj_rules_play(&g, a, KJ_ROCK, 2);
    kj_rules_play(&g, b, KJ_SCISSORS, 2);      // a 4★ b 2★
    kj_rules_challenge(&g, c, kj_no_of(bot), 3);
    kj_rules_respond(&g, bot, true, 3);
    kj_rules_play(&g, c, KJ_PAPER, 4);          // c 已出暗牌，对决进行中
    kj_rules_remove(&g, b, 5);                  // 留一个空座位，测试稀疏编号

    // 小缓冲区：失败且不越界
    CHECK_EQ(kj_persist_save(&g, buf, 10), 0);
    size_t n = kj_persist_save(&g, buf, sizeof(buf));
    CHECK_EQ(n, KJ_PERSIST_HEADER + 3 * KJ_PERSIST_RECORD + 4);

    kj_rules_init(&h);
    CHECK(kj_persist_load(&h, buf, n, 100000));
    CHECK_EQ(h.phase, KJ_PHASE_RUNNING);
    CHECK_EQ(h.game_id, g.game_id);
    CHECK_EQ(h.next_duel_id, g.next_duel_id);
    CHECK_EQ(kj_rules_player_count(&h), 3);
    CHECK(!h.players[b].used);
    CHECK_EQ(h.players[a].stars, 4);
    CHECK_EQ(h.players[a].cards[KJ_ROCK], 3);
    CHECK_EQ(h.players[a].wins, 1);
    CHECK(memcmp(h.players[a].mac, m1, 6) == 0);
    CHECK_EQ(kj_rules_find_mac(&h, m3), c);
    // 进行中的对决回到空闲，暗牌没扣
    CHECK_EQ(h.players[c].status, KJ_ST_IDLE);
    CHECK_EQ(h.players[c].cards[KJ_PAPER], 4);
    CHECK_EQ(h.players[c].locked, KJ_CARD_NONE);
    CHECK_EQ(h.players[bot].status, KJ_ST_IDLE);
    CHECK(h.players[bot].is_bot);
    // 真人选手需要重新听到心跳才算在线；电脑选手直接在线
    CHECK(!kj_rules_online(&h, a, 100000));
    CHECK(kj_rules_online(&h, bot, 100000));
    // 视图版本前移
    CHECK_EQ((uint16_t)(h.players[a].view_ver - g.players[a].view_ver), KJ_PERSIST_VER_BUMP);
    // 恢复后可以继续对局
    kj_rules_seen(&h, a, -40, 100001);
    kj_rules_seen(&h, c, -40, 100001);
    CHECK_EQ(kj_rules_challenge(&h, a, kj_no_of(c), 100002), KJ_N_NONE);

    // 损坏：任何一个字节翻转都被拒收，且不改动目标状态
    kj_game_t before = h;
    for (size_t i = 0; i < n; i++) {
        uint8_t saved = buf[i];
        buf[i] ^= 0x41;
        CHECK(!kj_persist_load(&h, buf, n, 0));
        buf[i] = saved;
    }
    CHECK(memcmp(&before, &h, sizeof(h)) == 0);
    CHECK(!kj_persist_load(&h, buf, n - 1, 0));
    CHECK(!kj_persist_load(&h, buf, 3, 0));

    // 空赌局也能往返
    kj_rules_init(&g);
    n = kj_persist_save(&g, buf, sizeof(buf));
    CHECK(kj_persist_load(&h, buf, n, 0));
    CHECK_EQ(kj_rules_player_count(&h), 0);
    CHECK_EQ(h.phase, KJ_PHASE_LOBBY);
    KJ_TEST_DONE("test_kj_persist");
}
