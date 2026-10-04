// tests/test_kj_proto.c —— 无线帧编解码：往返、长度、魔数 / 版本 / 越界字段拒收、位图。
#include "kj_proto.h"
#include "kj_test.h"

#include <string.h>

static void test_roundtrip(void)
{
    uint8_t buf[KJ_FRAME_MAX];
    kj_frame_t in, out;

    memset(&in, 0, sizeof(in));
    in.type = KJ_F_ROOM;
    in.room = 0xA3F2;
    in.u.room_info.phase = KJ_PHASE_RUNNING;
    in.u.room_info.game_id = 513;
    in.u.room_info.phase_ver = 0xBEEF;
    in.u.room_info.seated = 128;
    in.u.room_info.online = 77;
    kj_bit_set(in.u.room_info.avail, 1);
    kj_bit_set(in.u.room_info.avail, 128);
    kj_bit_set(in.u.room_info.bots, 64);
    size_t n = kj_proto_encode(&in, buf, sizeof(buf));
    CHECK_EQ(n, KJ_FRAME_HEADER + 39);
    CHECK_EQ(buf[0], 'K');
    CHECK_EQ(buf[1], 'J');
    CHECK_EQ(buf[4], 0xF2);   // 小端
    CHECK_EQ(buf[5], 0xA3);
    CHECK(kj_proto_decode(buf, n, &out));
    CHECK_EQ(out.type, KJ_F_ROOM);
    CHECK_EQ(out.room, 0xA3F2);
    CHECK_EQ(out.u.room_info.game_id, 513);
    CHECK_EQ(out.u.room_info.phase_ver, 0xBEEF);
    CHECK_EQ(out.u.room_info.seated, 128);
    CHECK(kj_bit_get(out.u.room_info.avail, 1));
    CHECK(kj_bit_get(out.u.room_info.avail, 128));
    CHECK(!kj_bit_get(out.u.room_info.avail, 2));
    CHECK(kj_bit_get(out.u.room_info.bots, 64));
    CHECK(!kj_bit_get(out.u.room_info.bots, 0));     // 编号 0 永远为假
    CHECK(!kj_bit_get(out.u.room_info.bots, 200));   // 越界永远为假

    memset(&in, 0, sizeof(in));
    in.type = KJ_F_HELLO;
    in.room = 7;
    in.u.hello.no = 12;
    in.u.hello.flags = KJ_HELLO_JOINED;
    in.u.hello.view_ver = 65535;
    n = kj_proto_encode(&in, buf, sizeof(buf));
    CHECK_EQ(n, KJ_FRAME_HEADER + 4);
    CHECK(kj_proto_decode(buf, n, &out));
    CHECK_EQ(out.u.hello.no, 12);
    CHECK_EQ(out.u.hello.view_ver, 65535);

    memset(&in, 0, sizeof(in));
    in.type = KJ_F_REQ;
    in.room = 7;
    in.u.req.seq = 0x1234;
    in.u.req.op = KJ_OP_PLAY;
    in.u.req.arg = KJ_PAPER;
    n = kj_proto_encode(&in, buf, sizeof(buf));
    CHECK(kj_proto_decode(buf, n, &out));
    CHECK_EQ(out.u.req.seq, 0x1234);
    CHECK_EQ(out.u.req.op, KJ_OP_PLAY);
    CHECK_EQ(out.u.req.arg, KJ_PAPER);

    memset(&in, 0, sizeof(in));
    in.type = KJ_F_VIEW;
    in.room = 7;
    kj_view_t *v = &in.u.view;
    v->view_ver = 300;
    v->ack_seq = 0xFFFE;
    v->game_id = 2;
    v->duel_id = 40;
    v->res_duel_id = 39;
    v->no = 128;
    v->phase = KJ_PHASE_RUNNING;
    v->status = KJ_ST_DUEL;
    v->cards[0] = 4;
    v->cards[1] = 0;
    v->cards[2] = 3;
    v->stars = 9;
    v->peer_no = 5;
    v->peer_is_bot = 1;
    v->my_lock = KJ_CARD_NONE;
    v->peer_locked = 1;
    v->notice = KJ_N_TIMEOUT;
    v->notice_seq = 200;
    v->res_my = KJ_ROCK;
    v->res_opp = KJ_SCISSORS;
    v->res_outcome = KJ_OUT_WIN;
    v->res_opp_no = 5;
    v->final_reason = KJ_FINAL_NONE;
    v->wins = 3;
    v->losses = 1;
    v->draws = 2;
    v->deadline_s = 17;
    n = kj_proto_encode(&in, buf, sizeof(buf));
    CHECK_EQ(n, KJ_FRAME_HEADER + 32);
    CHECK(n <= 250);   // ESP-NOW v1 单帧上限
    CHECK(kj_proto_decode(buf, n, &out));
    CHECK(memcmp(&out.u.view, v, sizeof(*v)) == 0);
}

static void test_rejects(void)
{
    uint8_t buf[KJ_FRAME_MAX];
    kj_frame_t in, out;
    memset(&in, 0, sizeof(in));
    in.type = KJ_F_VIEW;
    in.u.view.no = 3;
    in.u.view.my_lock = KJ_CARD_NONE;
    in.u.view.res_my = KJ_CARD_NONE;
    in.u.view.res_opp = KJ_CARD_NONE;
    size_t n = kj_proto_encode(&in, buf, sizeof(buf));
    CHECK(kj_proto_decode(buf, n, &out));
    // 截断
    for (size_t cut = 0; cut < n; cut++) CHECK(!kj_proto_decode(buf, cut, &out));
    // 尾部多出的字节被允许（向后兼容的扩展字段）
    uint8_t longer[KJ_FRAME_MAX];
    memcpy(longer, buf, n);
    longer[n] = 0xAA;
    CHECK(kj_proto_decode(longer, n + 1, &out));
    // 魔数 / 版本 / 类型
    uint8_t bad[KJ_FRAME_MAX];
    memcpy(bad, buf, n);
    bad[0] = 'X';
    CHECK(!kj_proto_decode(bad, n, &out));
    memcpy(bad, buf, n);
    bad[2] = KJ_PROTO_VERSION + 1;
    CHECK(!kj_proto_decode(bad, n, &out));
    memcpy(bad, buf, n);
    bad[3] = 99;
    CHECK(!kj_proto_decode(bad, n, &out));
    // 越界字段：编号、状态、手牌数、牌型
    memcpy(bad, buf, n);
    bad[KJ_FRAME_HEADER + 10] = KJ_MAX_PLAYERS + 1;   // no
    CHECK(!kj_proto_decode(bad, n, &out));
    memcpy(bad, buf, n);
    bad[KJ_FRAME_HEADER + 12] = KJ_ST_COUNT;          // status
    CHECK(!kj_proto_decode(bad, n, &out));
    memcpy(bad, buf, n);
    bad[KJ_FRAME_HEADER + 13] = KJ_CARDS_PER_TYPE + 1; // cards[0]
    CHECK(!kj_proto_decode(bad, n, &out));
    memcpy(bad, buf, n);
    bad[KJ_FRAME_HEADER + 19] = 3;                    // my_lock 既不是牌也不是 NONE
    CHECK(!kj_proto_decode(bad, n, &out));
    // REQ 的操作码
    memset(&in, 0, sizeof(in));
    in.type = KJ_F_REQ;
    in.u.req.op = KJ_OP_JOIN;
    n = kj_proto_encode(&in, buf, sizeof(buf));
    buf[KJ_FRAME_HEADER + 2] = 0;
    CHECK(!kj_proto_decode(buf, n, &out));
    buf[KJ_FRAME_HEADER + 2] = KJ_OP_COUNT;
    CHECK(!kj_proto_decode(buf, n, &out));
    // 编码缓冲区不足
    CHECK_EQ(kj_proto_encode(&in, buf, 5), 0);
    CHECK(!kj_proto_decode(NULL, 10, &out));
}

static void test_outbox_and_seq(void)
{
    kj_outbox_t o;
    kj_outbox_clear(&o);
    kj_frame_t f = { .type = KJ_F_HELLO, .room = 1 };
    uint8_t mac[6] = { 1, 2, 3, 4, 5, 6 };
    for (int i = 0; i < KJ_OUTBOX_MAX; i++) CHECK(kj_outbox_push(&o, i % 2 ? mac : NULL, &f));
    CHECK(!kj_outbox_push(&o, mac, &f));
    CHECK_EQ(o.dropped, 1);
    CHECK(o.items[0].broadcast);
    CHECK_EQ(o.items[0].mac[0], 0xFF);
    CHECK(!o.items[1].broadcast);
    CHECK_EQ(o.items[1].mac[5], 6);

    CHECK(kj_seq_diff(5, 3) > 0);
    CHECK(kj_seq_diff(3, 5) < 0);
    CHECK(kj_seq_diff(1, 65535) > 0);   // 回绕
    CHECK_EQ(kj_seq_diff(9, 9), 0);
}

int main(void)
{
    test_roundtrip();
    test_rejects();
    test_outbox_and_seq();
    KJ_TEST_DONE("test_kj_proto");
}
