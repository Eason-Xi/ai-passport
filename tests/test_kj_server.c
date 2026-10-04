// tests/test_kj_server.c —— 庄家协议逻辑：入座 / 去重 / 非成员处理 / 视图重发上限 / 信标 / 电脑选手。
#include "kj_server.h"
#include "kj_test.h"

#include <string.h>

#define ROOM 0x1234

static kj_server_t s;
static kj_outbox_t out;

static size_t enc(const kj_frame_t *f, uint8_t *buf)
{
    return kj_proto_encode(f, buf, KJ_FRAME_MAX);
}

static void req(const uint8_t mac[6], uint16_t seq, uint8_t op, uint8_t arg, uint32_t now)
{
    kj_frame_t f = { .type = KJ_F_REQ, .room = ROOM };
    f.u.req.seq = seq;
    f.u.req.op = op;
    f.u.req.arg = arg;
    uint8_t buf[KJ_FRAME_MAX];
    size_t n = enc(&f, buf);
    kj_server_on_frame(&s, mac, -50, buf, n, now, &out);
}

static void hello(const uint8_t mac[6], uint8_t no, uint16_t ver, uint32_t now)
{
    kj_frame_t f = { .type = KJ_F_HELLO, .room = ROOM };
    f.u.hello.no = no;
    f.u.hello.flags = KJ_HELLO_JOINED;
    f.u.hello.view_ver = ver;
    uint8_t buf[KJ_FRAME_MAX];
    size_t n = enc(&f, buf);
    kj_server_on_frame(&s, mac, -50, buf, n, now, &out);
}

// 找出发给 mac 的最后一个视图
static bool last_view_to(const uint8_t mac[6], kj_view_t *v)
{
    bool found = false;
    for (int i = 0; i < out.count; i++) {
        kj_frame_t f;
        if (out.items[i].broadcast || memcmp(out.items[i].mac, mac, 6) != 0) continue;
        if (kj_proto_decode(out.items[i].data, out.items[i].len, &f) && f.type == KJ_F_VIEW) {
            *v = f.u.view;
            found = true;
        }
    }
    return found;
}

static int count_type(uint8_t type)
{
    int n = 0;
    for (int i = 0; i < out.count; i++) {
        kj_frame_t f;
        if (kj_proto_decode(out.items[i].data, out.items[i].len, &f) && f.type == type) n++;
    }
    return n;
}

int main(void)
{
    uint8_t a[6] = { 2, 0, 0, 0, 0, 1 }, b[6] = { 2, 0, 0, 0, 0, 2 }, x[6] = { 2, 0, 0, 0, 0, 9 };
    kj_view_t v;
    kj_server_init(&s, ROOM, 1);

    // 第一个 tick 立刻广播信标
    kj_outbox_clear(&out);
    kj_server_tick(&s, 0, &out);
    CHECK_EQ(count_type(KJ_F_ROOM), 1);
    kj_outbox_clear(&out);
    kj_server_tick(&s, 500, &out);
    CHECK_EQ(count_type(KJ_F_ROOM), 0);   // 无变化时按 1 s 周期
    kj_server_tick(&s, 1000, &out);
    CHECK_EQ(count_type(KJ_F_ROOM), 1);

    // 入座：立刻回视图，ack_seq 对上
    kj_outbox_clear(&out);
    req(a, 100, KJ_OP_JOIN, 0, 1100);
    CHECK(last_view_to(a, &v));
    CHECK_EQ(v.no, 1);
    CHECK_EQ(v.ack_seq, 100);
    CHECK_EQ(v.status, KJ_ST_WAITING);
    req(b, 7, KJ_OP_JOIN, 0, 1100);
    // 别的赌局号直接忽略
    kj_frame_t other = { .type = KJ_F_REQ, .room = ROOM + 1 };
    other.u.req.seq = 1;
    other.u.req.op = KJ_OP_JOIN;
    uint8_t buf[KJ_FRAME_MAX];
    size_t n = enc(&other, buf);
    kj_server_on_frame(&s, x, -50, buf, n, 1100, &out);
    CHECK_EQ(kj_rules_player_count(&s.game), 2);
    // 坏帧计数
    kj_server_on_frame(&s, x, -50, buf, 3, 1100, &out);
    CHECK_EQ(s.rx_bad, 1);

    // 状态变化后信标在最短间隔后就发（不等满 1 s）
    kj_outbox_clear(&out);
    kj_server_tick(&s, 1100 + KJ_ROOM_BEACON_MIN_MS, &out);
    CHECK_EQ(count_type(KJ_F_ROOM), 1);

    CHECK_EQ(kj_server_command(&s, KJ_CMD_START, 0, 1300), KJ_N_NONE);
    CHECK_EQ(kj_server_command(&s, KJ_CMD_START, 0, 1300), KJ_N_INVALID);

    // 挑战；重复的同一序号不会执行两次
    kj_outbox_clear(&out);
    req(a, 101, KJ_OP_CHALLENGE, 2, 1400);
    CHECK_EQ(s.game.players[1].status, KJ_ST_CHALLENGED);
    CHECK(last_view_to(b, &v));                  // 被挑战者马上收到视图
    CHECK_EQ(v.status, KJ_ST_CHALLENGED);
    CHECK_EQ(v.peer_no, 1);
    req(b, 8, KJ_OP_ACCEPT, 0, 1500);
    CHECK_EQ(s.game.players[0].status, KJ_ST_DUEL);
    req(a, 102, KJ_OP_PLAY, KJ_ROCK, 1600);
    kj_outbox_clear(&out);
    req(a, 102, KJ_OP_PLAY, KJ_PAPER, 1650);     // 重发（同序号）：只补发视图
    CHECK_EQ(s.game.players[0].locked, KJ_ROCK);
    CHECK(last_view_to(a, &v));
    CHECK_EQ(v.ack_seq, 102);
    req(a, 101, KJ_OP_CANCEL, 0, 1660);          // 更旧的序号：忽略
    CHECK_EQ(s.game.players[0].status, KJ_ST_DUEL);
    // 失败的请求也会确认，并带上原因
    kj_outbox_clear(&out);
    req(b, 9, KJ_OP_PLAY, 9, 1700);
    CHECK(last_view_to(b, &v));
    CHECK_EQ(v.ack_seq, 9);
    CHECK_EQ(v.notice, KJ_N_INVALID);
    req(b, 10, KJ_OP_PLAY, KJ_SCISSORS, 1800);
    CHECK_EQ(s.game.players[0].stars, 4);
    CHECK_EQ(s.game.players[1].stars, 2);

    // 选手重启后序号从头：入座请求重置去重基准，之后的小序号照常执行
    req(b, 1, KJ_OP_JOIN, 0, 1900);
    CHECK_EQ(s.game.players[1].last_req_seq, 1);
    req(b, 2, KJ_OP_CHALLENGE, 1, 1950);
    CHECK_EQ(s.game.players[1].status, KJ_ST_CHALLENGING);
    req(b, 3, KJ_OP_CANCEL, 0, 1960);

    // 不在册的设备：心跳 / 请求都会收到 no=0 的视图
    kj_outbox_clear(&out);
    hello(x, 5, 3, 2000);
    CHECK(last_view_to(x, &v));
    CHECK_EQ(v.no, 0);
    kj_outbox_clear(&out);
    req(x, 50, KJ_OP_CHALLENGE, 1, 2000);
    CHECK(last_view_to(x, &v));
    CHECK_EQ(v.no, 0);

    // 视图重发：没有确认时每 400 ms 重发，最多 KJ_VIEW_RETRY_MAX 次；心跳确认后停止
    kj_rules_touch(&s.game, 0);
    uint32_t t = 3000;
    int sent = 0;
    for (int k = 0; k < 40; k++, t += 100) {
        kj_outbox_clear(&out);
        hello(b, 2, s.game.players[1].view_ver, t);   // b 一直确认；a 不发心跳但仍在线
        if (k % 10 == 0) kj_rules_seen(&s.game, 0, -50, t);
        kj_server_tick(&s, t, &out);
        sent += last_view_to(a, &v) ? 1 : 0;
    }
    CHECK_EQ(sent, KJ_VIEW_RETRY_MAX);   // 首发 + 重发，总数封顶
    kj_outbox_clear(&out);
    hello(a, 1, (uint16_t)(s.game.players[0].view_ver - 1), t);   // 过期版本：立刻补发
    CHECK(last_view_to(a, &v));
    CHECK_EQ(v.view_ver, s.game.players[0].view_ver);
    hello(a, 1, s.game.players[0].view_ver, t + 10);
    kj_outbox_clear(&out);
    kj_server_tick(&s, t + 1000, &out);
    CHECK(!last_view_to(a, &v));

    // 赌局满员：入座被拒并说明原因
    kj_server_init(&s, ROOM, 2);
    for (int i = 0; i < KJ_MAX_PLAYERS; i++) kj_server_command(&s, KJ_CMD_BOT_ADD, 0, 0);
    CHECK_EQ(kj_server_command(&s, KJ_CMD_BOT_ADD, 0, 0), KJ_N_FULL);
    kj_outbox_clear(&out);
    req(x, 1, KJ_OP_JOIN, 0, 10);
    CHECK(last_view_to(x, &v));
    CHECK_EQ(v.no, 0);
    CHECK_EQ(v.notice, KJ_N_FULL);
    CHECK_EQ(kj_server_command(&s, KJ_CMD_KICK, 0, 0), KJ_N_INVALID);
    CHECK_EQ(kj_server_command(&s, KJ_CMD_KICK, 128, 0), KJ_N_NONE);
    CHECK_EQ(kj_server_command(&s, KJ_CMD_SYNC, 0, 0), KJ_N_NONE);

    // 信标位图：空闲名单与电脑选手
    kj_server_init(&s, ROOM, 3);
    req(a, 1, KJ_OP_JOIN, 0, 0);
    kj_server_command(&s, KJ_CMD_BOT_ADD, 0, 0);
    kj_server_command(&s, KJ_CMD_BOT_ADD, 0, 0);
    kj_server_command(&s, KJ_CMD_START, 0, 0);
    kj_room_t ri;
    kj_server_room_info(&s, 100, &ri);
    CHECK_EQ(ri.seated, 3);
    CHECK(kj_bit_get(ri.avail, 1) && kj_bit_get(ri.avail, 2) && kj_bit_get(ri.avail, 3));
    CHECK(!kj_bit_get(ri.bots, 1) && kj_bit_get(ri.bots, 2));
    kj_server_room_info(&s, 100 + KJ_ONLINE_TIMEOUT_MS, &ri);
    CHECK(!kj_bit_get(ri.avail, 1));   // 真人离线后不在空闲名单
    CHECK(kj_bit_get(ri.avail, 2));

    // 电脑选手：应战、出牌、只剩电脑时彼此对决
    req(a, 2, KJ_OP_CHALLENGE, 2, 200);
    CHECK_EQ(s.game.players[1].status, KJ_ST_CHALLENGED);
    t = 200;
    for (int k = 0; k < 40 && s.game.players[1].status == KJ_ST_CHALLENGED; k++) {
        t += 100;
        kj_outbox_clear(&out);
        kj_server_tick(&s, t, &out);
    }
    CHECK_EQ(s.game.players[1].status, KJ_ST_DUEL);
    CHECK(t - 200 >= 1200);   // 有思考延迟
    req(a, 3, KJ_OP_PLAY, KJ_PAPER, t);
    for (int k = 0; k < 60 && s.game.players[0].status == KJ_ST_DUEL; k++) {
        t += 100;
        kj_outbox_clear(&out);
        kj_server_tick(&s, t, &out);
    }
    CHECK_EQ(s.game.players[0].status, KJ_ST_IDLE);
    CHECK_EQ(s.game.players[1].res_my != KJ_CARD_NONE, 1);
    long before = s.game.next_duel_id;
    for (int k = 0; k < 400; k++) {
        t += 100;
        kj_outbox_clear(&out);
        kj_server_tick(&s, t, &out);
    }
    CHECK(s.game.next_duel_id > before);   // 两个电脑选手自己打起来了
    CHECK_EQ(s.game.players[0].status, KJ_ST_IDLE);   // 真人没被电脑骚扰

    // 选牌偏好：只剩一种牌时一定出那张
    kj_player_t p = { 0 };
    p.cards[KJ_PAPER] = 2;
    uint32_t rng = 5;
    for (int k = 0; k < 20; k++) CHECK_EQ(kj_bot_choose(&p, &rng), KJ_PAPER);
    memset(p.cards, 0, sizeof(p.cards));
    CHECK_EQ(kj_bot_choose(&p, &rng), KJ_CARD_NONE);
    KJ_TEST_DONE("test_kj_server");
}
