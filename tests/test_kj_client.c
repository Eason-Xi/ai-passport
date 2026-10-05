// tests/test_kj_client.c —— 选手协议逻辑：找赌局、入座重试与失败、视图确认、被移出、对手名单排序、
// 请求的开机号与 age、乱序视图丢弃、心跳单播给庄家。
#include "kj_client.h"
#include "kj_test.h"

#include <string.h>

static kj_client_t c;
static kj_outbox_t out;

static void feed(const uint8_t mac[6], const kj_frame_t *f, uint32_t now)
{
    uint8_t buf[KJ_FRAME_MAX];
    size_t n = kj_proto_encode(f, buf, sizeof(buf));
    kj_client_on_frame(&c, mac, buf, n, now);
}

static void beacon(const uint8_t mac[6], uint16_t room, uint32_t now, const uint8_t *avail_nos, int n,
                   uint8_t bot_no)
{
    kj_frame_t f = { .type = KJ_F_ROOM, .room = room };
    f.u.room_info.phase = KJ_PHASE_RUNNING;
    f.u.room_info.seated = 9;
    for (int i = 0; i < n; i++) kj_bit_set(f.u.room_info.avail, avail_nos[i]);
    if (bot_no) kj_bit_set(f.u.room_info.bots, bot_no);
    feed(mac, &f, now);
}

static int count(uint8_t type, bool broadcast)
{
    int n = 0;
    for (int i = 0; i < out.count; i++) {
        kj_frame_t f;
        if (kj_proto_decode(out.items[i].data, out.items[i].len, &f) && f.type == type &&
            out.items[i].broadcast == broadcast) {
            n++;
        }
    }
    return n;
}

static bool last_req(kj_req_t *r)
{
    bool found = false;
    for (int i = 0; i < out.count; i++) {
        kj_frame_t f;
        if (kj_proto_decode(out.items[i].data, out.items[i].len, &f) && f.type == KJ_F_REQ) {
            *r = f.u.req;
            found = true;
        }
    }
    return found;
}

int main(void)
{
    uint8_t h1[6] = { 0x24, 1, 1, 1, 1, 1 }, h2[6] = { 0x24, 2, 2, 2, 2, 2 };
    kj_client_init(&c, 42);
    kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];

    // 两个赌局，按赌局号排序；过期后消失
    beacon(h2, 0x2222, 100, NULL, 0, 0);
    beacon(h1, 0x1111, 120, NULL, 0, 0);
    CHECK_EQ(kj_client_rooms(&c, 200, rooms, KJ_CLIENT_MAX_ROOMS), 2);
    CHECK_EQ(rooms[0].room, 0x1111);
    CHECK_EQ(kj_client_rooms(&c, 200, rooms, 1), 1);
    CHECK_EQ(kj_client_rooms(&c, 100 + KJ_CLIENT_ROOM_TTL_MS + 1, rooms, KJ_CLIENT_MAX_ROOMS), 1);
    CHECK_EQ(kj_client_rooms(&c, 120 + KJ_CLIENT_ROOM_TTL_MS + 1, rooms, KJ_CLIENT_MAX_ROOMS), 0);
    // 未入座时不广播心跳、不能发请求
    kj_outbox_clear(&out);
    kj_client_tick(&c, 300, &out);
    CHECK_EQ(out.count, 0);
    CHECK(!kj_client_request(&c, KJ_OP_CHALLENGE, 2, 300));
    CHECK(!kj_client_join(&c, 0x9999, 300));

    // 入座：请求按 250 ms 重发，12 次无回应后放弃并回到找赌局
    CHECK(kj_client_join(&c, 0x1111, 300));
    CHECK_EQ(c.link, KJ_LINK_JOINING);
    int reqs = 0;
    for (uint32_t t = 300; t < 300 + 5000; t += 50) {
        kj_outbox_clear(&out);
        kj_client_tick(&c, t, &out);
        reqs += count(KJ_F_REQ, false);
    }
    CHECK_EQ(reqs, KJ_CLIENT_REQ_TRIES);
    CHECK(c.boot != 0);
    CHECK(kj_client_take_req_failed(&c));
    CHECK(!kj_client_take_req_failed(&c));
    CHECK_EQ(c.link, KJ_LINK_IDLE);

    // 再次入座并收到视图
    beacon(h1, 0x1111, 6000, NULL, 0, 0);
    CHECK(kj_client_join(&c, 0x1111, 6000));
    kj_outbox_clear(&out);
    kj_client_tick(&c, 6000, &out);
    kj_req_t r;
    CHECK(last_req(&r));
    CHECK_EQ(r.op, KJ_OP_JOIN);
    CHECK_EQ(r.boot, c.boot);
    CHECK_EQ(r.age_ms, 0);
    CHECK(memcmp(out.items[0].mac, h1, 6) == 0);
    // 重发时 age 记录请求已经在路上多久
    kj_outbox_clear(&out);
    kj_client_tick(&c, 6000 + KJ_CLIENT_REQ_RETRY_MS, &out);
    CHECK(last_req(&r));
    CHECK_EQ(r.age_ms, KJ_CLIENT_REQ_RETRY_MS);
    kj_frame_t vf = { .type = KJ_F_VIEW, .room = 0x1111 };
    vf.u.view.no = 4;
    vf.u.view.view_ver = 10;
    vf.u.view.ack_seq = r.seq;
    vf.u.view.phase = KJ_PHASE_RUNNING;
    vf.u.view.status = KJ_ST_IDLE;
    vf.u.view.stars = 3;
    vf.u.view.my_lock = vf.u.view.res_my = vf.u.view.res_opp = KJ_CARD_NONE;
    // 来自别的设备（冒充）的视图不接受
    feed(h2, &vf, 6010);
    CHECK_EQ(c.link, KJ_LINK_JOINING);
    feed(h1, &vf, 6020);
    CHECK_EQ(c.link, KJ_LINK_JOINED);
    CHECK(!c.pending);
    CHECK(kj_client_take_view_changed(&c));
    CHECK(!kj_client_take_view_changed(&c));
    CHECK(kj_client_connected(&c, 6100));
    // 收到视图后立即单播确认心跳，带上版本号
    kj_outbox_clear(&out);
    kj_client_tick(&c, 6030, &out);
    CHECK(count(KJ_F_HELLO, false) >= 1);
    // 周期心跳只单播给庄家（经 hub 中继，别的选手收不到也不需要）
    kj_outbox_clear(&out);
    for (uint32_t t = 6040; t < 6040 + 2000; t += 50) kj_client_tick(&c, t, &out);
    CHECK(count(KJ_F_HELLO, false) >= 1);
    CHECK_EQ(count(KJ_F_HELLO, true), 0);
    CHECK(memcmp(out.items[out.count - 1].mac, h1, 6) == 0);
    kj_frame_t hf;
    CHECK(kj_proto_decode(out.items[out.count - 1].data, out.items[out.count - 1].len, &hf));
    CHECK_EQ(hf.u.hello.view_ver, 10);
    CHECK_EQ(hf.u.hello.no, 4);
    CHECK(hf.u.hello.flags & KJ_HELLO_JOINED);

    // 请求：一次只能有一个未确认的
    CHECK(kj_client_request(&c, KJ_OP_CHALLENGE, 7, 8000));
    CHECK(!kj_client_request(&c, KJ_OP_CHALLENGE, 8, 8000));
    CHECK(!kj_client_request(&c, KJ_OP_JOIN, 0, 8000));
    kj_outbox_clear(&out);
    kj_client_tick(&c, 8000, &out);
    CHECK(last_req(&r));
    CHECK_EQ(r.arg, 7);
    vf.u.view.ack_seq = (uint16_t)(r.seq - 1);   // 旧确认不算
    feed(h1, &vf, 8050);
    CHECK(c.pending);
    vf.u.view.ack_seq = r.seq;
    vf.u.view.view_ver = 11;
    feed(h1, &vf, 8100);
    CHECK(!c.pending);

    // 乱序：同一 epoch 内更旧的视图被丢弃，但仍回心跳；epoch 变了（庄家重启）照常接受
    vf.u.view.epoch = 0x0E01;
    vf.u.view.view_ver = 20;
    vf.u.view.stars = 4;
    feed(h1, &vf, 8110);
    CHECK_EQ(c.view.stars, 4);
    kj_client_take_view_changed(&c);
    kj_outbox_clear(&out);
    kj_client_tick(&c, 8111, &out);   // 消耗掉确认心跳
    vf.u.view.view_ver = 19;
    vf.u.view.stars = 2;
    feed(h1, &vf, 8120);
    CHECK_EQ(c.view.stars, 4);
    CHECK_EQ(c.view.view_ver, 20);
    CHECK(!kj_client_take_view_changed(&c));
    CHECK(c.ack_due);
    vf.u.view.epoch = 0x0E02;          // 庄家重启后版本号可以从任何值开始
    vf.u.view.view_ver = 5;
    vf.u.view.stars = 3;
    feed(h1, &vf, 8130);
    CHECK_EQ(c.view.stars, 3);
    CHECK_EQ(c.view.view_ver, 5);
    vf.u.view.view_ver = 11;           // 后面的测试沿用这个视图

    // 对手名单：真人在前、电脑选手在后，各自按编号；不含自己；别的赌局的心跳不影响
    uint8_t avail[] = { 2, 4, 5, 9, 30 };
    beacon(h1, 0x1111, 8200, avail, 5, 2);
    kj_frame_t hello = { .type = KJ_F_HELLO, .room = 0x2222 };
    hello.u.hello.flags = KJ_HELLO_JOINED;
    hello.u.hello.no = 9;
    feed(h2, &hello, 8200);
    kj_opponent_t opp[8];
    int n = kj_client_opponents(&c, opp, 8);
    CHECK_EQ(n, 4);
    CHECK_EQ(opp[0].no, 5);
    CHECK_EQ(opp[1].no, 9);
    CHECK_EQ(opp[2].no, 30);
    CHECK_EQ(opp[3].no, 2);
    CHECK(opp[3].is_bot);
    // 容量有限时保留真人
    n = kj_client_opponents(&c, opp, 1);
    CHECK_EQ(n, 1);
    CHECK_EQ(opp[0].no, 5);

    // 庄家失联判定
    CHECK(!kj_client_connected(&c, 8200 + KJ_CLIENT_HOST_TTL_MS + 1));

    // 被移出：no=0 的视图让我们回到找赌局，并带出原因
    kj_frame_t kick = { .type = KJ_F_VIEW, .room = 0x1111 };
    kick.u.view.my_lock = kick.u.view.res_my = kick.u.view.res_opp = KJ_CARD_NONE;
    kick.u.view.notice = KJ_N_FULL;
    feed(h1, &kick, 9000);
    uint8_t why = 0;
    CHECK(kj_client_take_kicked(&c, &why));
    CHECK_EQ(why, KJ_N_FULL);
    CHECK_EQ(c.link, KJ_LINK_IDLE);
    CHECK(!kj_client_take_kicked(&c, &why));
    KJ_TEST_DONE("test_kj_client");
}
