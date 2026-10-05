// tests/test_kj_hubc.c —— 设备侧 hub 客户端：寻找（广播 / 上次地址交替）、保活与失联、昵称以 hub 为准、
// 昵称查询的批量 / 去重 / 重发 / 作废、扫码登记的状态推进、token 生成。
#include "kj_hubc.h"
#include "kj_proto.h"
#include "kj_test.h"

#include <string.h>

static const uint8_t MAC[6] = { 0x24, 0x6F, 0x28, 0x01, 0x02, 0x03 };
static const uint32_t HUB_IP = 0xC0A8010A;   // 192.168.1.10
static kj_hubc_t h;

static void feed(uint8_t kind, uint16_t room, const uint8_t *p, size_t n, uint32_t now)
{
    kh_env_t e = { .kind = kind, .room = room, .payload = p, .len = (uint16_t)n };
    kj_hubc_note_rx(&h, now);
    kj_hubc_on_msg(&h, &e, HUB_IP, now);
}

static void offer(uint32_t roster_rev, uint8_t flags, uint32_t name_rev, const char *name, uint32_t now)
{
    kh_offer_t o = { .hub_id = 77, .http_port = 8080, .tcp_port = 9000, .roster_rev = roster_rev, .flags = flags,
                     .name_rev = name_rev };
    strcpy(o.name, name);
    uint8_t p[64];
    feed(KH_K_OFFER, 0, p, kh_offer_encode(&o, p, sizeof(p)), now);
}

static int tick(uint32_t now, kj_hubc_out_t *out)
{
    return kj_hubc_tick(&h, now, out, KJ_HUBC_OUT_MAX);
}

static int count_kind(const kj_hubc_out_t *out, int n, uint8_t kind)
{
    int c = 0;
    for (int i = 0; i < n; i++) c += out[i].kind == kind;
    return c;
}

static void test_discovery(void)
{
    kj_hubc_out_t out[KJ_HUBC_OUT_MAX];
    kj_hubc_init(&h, MAC, 0xB007, "1.1.0", "", 0, 0);
    CHECK_EQ(kj_hubc_state(&h, 0), KJ_HUB_SEARCHING);
    // 没有上次的地址：每秒广播一次 DISCOVER
    int n = tick(100, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].kind, KH_K_DISCOVER);
    CHECK_EQ(out[0].ip, 0);
    kh_discover_t d;
    CHECK(kh_discover_decode(out[0].data, out[0].len, &d));
    CHECK_EQ(d.boot, 0xB007);
    CHECK_EQ(d.game_proto, KJ_PROTO_VERSION);
    CHECK(strcmp(d.fw, "1.1.0") == 0);
    CHECK_EQ(tick(600, out), 0);
    CHECK_EQ(tick(1100, out), 1);
    // 有上次的地址：单播与广播交替
    kj_hubc_init(&h, MAC, 1, "fw", "", 0, 0x0A000005);
    uint32_t ips[4];
    for (int k = 0; k < 4; k++) {
        CHECK_EQ(tick(1000u * (uint32_t)k + 1, out), 1);
        ips[k] = out[0].ip;
    }
    CHECK(ips[0] != ips[1]);
    CHECK(ips[0] == ips[2] && ips[1] == ips[3]);
    CHECK((ips[0] == 0 && ips[1] == 0x0A000005) || (ips[1] == 0 && ips[0] == 0x0A000005));

    // 收到 OFFER：找到 hub，之后每 5 s 单播保活
    offer(1, 0, 0, "", 5000);
    CHECK_EQ(kj_hubc_state(&h, 5000), KJ_HUB_OK);
    CHECK_EQ(h.hub_ip, HUB_IP);
    CHECK_EQ(h.http_port, 8080);
    CHECK_EQ(h.tcp_port, 9000);
    CHECK_EQ(tick(5001, out), 0);
    CHECK_EQ(tick(5000 + KJ_HUBC_KEEPALIVE_MS, out), 1);
    CHECK_EQ(out[0].ip, HUB_IP);
    // 角色变化：马上再发一次，让 hub 知道这是庄家
    kj_hubc_set_role(&h, KH_ROLE_HOST, 0x1234);
    n = tick(5000 + KJ_HUBC_KEEPALIVE_MS + 10, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].room, 0x1234);
    CHECK(kh_discover_decode(out[0].data, out[0].len, &d));
    CHECK_EQ(d.role, KH_ROLE_HOST);
    // 中继的游戏帧也算 hub 还活着
    kj_hubc_note_rx(&h, 15000);
    CHECK_EQ(kj_hubc_state(&h, 15000 + KJ_HUBC_LOST_MS - 1), KJ_HUB_OK);
    // 12 s 没收到任何东西：失联，回到寻找（立即发一次）
    CHECK_EQ(kj_hubc_state(&h, 15000 + KJ_HUBC_LOST_MS), KJ_HUB_SEARCHING);
    n = tick(15000 + KJ_HUBC_LOST_MS, out);
    CHECK_EQ(n, 1);
    CHECK_EQ(out[0].kind, KH_K_DISCOVER);
    CHECK_EQ(h.state, KJ_HUB_SEARCHING);
    kj_hubc_note_rx(&h, 30000);   // 寻找中的杂包不会"复活"连接
    CHECK_EQ(kj_hubc_state(&h, 30000), KJ_HUB_SEARCHING);
}

static void test_my_name(void)
{
    kj_hubc_out_t out[KJ_HUBC_OUT_MAX];
    // 本机有昵称：DISCOVER 里带上（hub 登记表丢了可以补回）
    kj_hubc_init(&h, MAC, 1, "fw", "\xE5\xB0\x8F\xE6\x98\x8E", 3, 0);
    CHECK_EQ(tick(0, out), 1);
    kh_discover_t d;
    CHECK(kh_discover_decode(out[0].data, out[0].len, &d));
    CHECK(strcmp(d.name, "\xE5\xB0\x8F\xE6\x98\x8E") == 0);
    CHECK_EQ(d.name_rev, 3);
    // 广播的 OFFER（不带"对你有效"）不改本机昵称
    offer(1, 0, 9, "X", 100);
    CHECK(!kj_hubc_take_my_name_changed(&h));
    // hub 说了算：改名 / 清空
    offer(1, KH_OFFER_NAME_VALID, 4, "Amy", 200);
    CHECK(kj_hubc_take_my_name_changed(&h));
    CHECK(!kj_hubc_take_my_name_changed(&h));
    CHECK(strcmp(h.my_name, "Amy") == 0);
    CHECK_EQ(h.my_rev, 4);
    offer(1, KH_OFFER_NAME_VALID, 4, "Amy", 300);   // 相同：不算变化
    CHECK(!kj_hubc_take_my_name_changed(&h));
    offer(1, KH_OFFER_NAME_VALID, 5, "", 400);
    CHECK(kj_hubc_take_my_name_changed(&h));
    CHECK_EQ(h.my_name[0], '\0');
    // 协议不兼容
    offer(1, KH_OFFER_INCOMPATIBLE, 0, "", 500);
    CHECK(h.incompatible);
}

static void names_reply(uint32_t roster_rev, uint16_t room, const uint8_t *nos, int n, uint32_t now)
{
    kh_names_t m = { .roster_rev = roster_rev, .count = (uint8_t)n };
    for (int i = 0; i < n; i++) {
        m.e[i].no = nos[i];
        if (nos[i] == 9) {
            m.e[i].flags = KH_NAME_BOT | KH_NAME_UNKNOWN;
        } else {
            snprintf(m.e[i].name, sizeof(m.e[i].name), "P%u", nos[i]);
        }
    }
    uint8_t p[256];
    feed(KH_K_NAMES, room, p, kh_names_encode(&m, p, sizeof(p)), now);
}

static void test_names(void)
{
    kj_hubc_out_t out[KJ_HUBC_OUT_MAX];
    kj_hubc_init(&h, MAC, 1, "fw", "", 0, 0);
    const char *name;
    uint8_t flags;
    // 没找到 hub 时只排队，不发查询
    CHECK(!kj_hubc_name(&h, 0x22, 3, 0, &name, &flags));
    CHECK(name == NULL);
    CHECK_EQ(count_kind(out, tick(0, out), KH_K_NAME_GET), 0);
    offer(5, 0, 0, "", 100);
    // 同一个编号只排一次；最多 6 个一批
    for (uint8_t no = 1; no <= 9; no++) kj_hubc_name(&h, 0x22, no, 110, NULL, NULL);
    kj_hubc_name(&h, 0x22, 3, 110, NULL, NULL);
    int n = tick(120, out);
    CHECK_EQ(count_kind(out, n, KH_K_NAME_GET), 1);
    uint8_t nos[KH_NAMES_MAX];
    int want = -1;
    for (int i = 0; i < n; i++) {
        if (out[i].kind == KH_K_NAME_GET) {
            want = kh_name_get_decode(out[i].data, out[i].len, nos);
            CHECK_EQ(out[i].room, 0x22);
            CHECK_EQ(out[i].ip, HUB_IP);
        }
    }
    CHECK_EQ(want, KH_NAMES_MAX);
    // 没回复：1 s 后重发
    CHECK_EQ(count_kind(out, tick(500, out), KH_K_NAME_GET), 0);
    CHECK_EQ(count_kind(out, tick(120 + KJ_HUBC_NAME_RETRY_MS, out), KH_K_NAME_GET), 1);
    // 回复：缓存，界面可直接用；电脑选手 / 没登记的带标志
    names_reply(5, 0x22, nos, want, 1200);
    CHECK(kj_hubc_name(&h, 0x22, nos[0], 1300, &name, &flags));
    CHECK(name && name[0] == 'P');
    CHECK_EQ(h.want_n, 0);
    uint8_t nine = 9;
    kj_hubc_name(&h, 0x22, 9, 1300, NULL, NULL);
    names_reply(5, 0x22, &nine, 1, 1310);
    CHECK(kj_hubc_name(&h, 0x22, 9, 1320, &name, &flags));
    CHECK_EQ(flags, KH_NAME_BOT | KH_NAME_UNKNOWN);
    CHECK_EQ(name[0], '\0');
    // 别的赌局的同一编号不混用
    CHECK(!kj_hubc_name(&h, 0x33, nos[0], 1330, &name, &flags));
    // roster_rev 变了（有人登记 / 改名 / 换座位）：整批作废
    offer(6, 0, 0, "", 1400);
    CHECK(!kj_hubc_name(&h, 0x22, nos[0], 1410, &name, &flags));
    // 缓存满了替换最久没用的
    offer(6, 0, 0, "", 1500);
    for (int k = 0; k < KJ_HUBC_NAME_CACHE + 4; k++) {
        uint8_t no = (uint8_t)(k + 10);
        names_reply(6, 0x44, &no, 1, 2000u + (uint32_t)k);
    }
    int cached = 0;
    for (int k = 0; k < KJ_HUBC_NAME_CACHE + 4; k++) {
        cached += kj_hubc_name(&h, 0x44, (uint8_t)(k + 10), 9000, NULL, NULL) ? 1 : 0;
    }
    CHECK_EQ(cached, KJ_HUBC_NAME_CACHE);
    CHECK(!kj_hubc_name(&h, 0x44, 10, 9000, NULL, NULL));   // 最早的被挤掉
}

static void test_registration(void)
{
    kj_hubc_out_t out[KJ_HUBC_OUT_MAX];
    kj_hubc_init(&h, MAC, 1, "fw", "", 0, 0);
    char token[KJ_REG_TOKEN_LEN + 1];
    kj_reg_token(0x12345678u, 0xABu, token);
    CHECK(kh_token_valid(token));
    char other[KJ_REG_TOKEN_LEN + 1];
    kj_reg_token(0x12345679u, 0xABu, other);
    CHECK(strcmp(token, other) != 0);
    kj_hubc_reg_start(&h, token, 0);
    CHECK_EQ(h.reg_state, KH_REG_WAITING);
    CHECK_EQ(count_kind(out, tick(0, out), KH_K_REG), 0);   // 还没找到 hub
    offer(1, 0, 0, "", 100);
    int n = tick(100, out);
    CHECK_EQ(count_kind(out, n, KH_K_REG), 1);
    CHECK_EQ(count_kind(out, tick(600, out), KH_K_REG), 0);
    CHECK_EQ(count_kind(out, tick(1100, out), KH_K_REG), 1);
    // hub 回状态：已打开网页 → 完成（带昵称）
    kh_reg_state_msg_t m = { .state = KH_REG_OPENED };
    memcpy(m.token, token, sizeof(m.token));
    uint8_t p[64];
    feed(KH_K_REG_STATE, 0, p, kh_reg_state_encode(&m, p, sizeof(p)), 1200);
    CHECK_EQ(h.reg_state, KH_REG_OPENED);
    // 别的 token 的回复不理
    kh_reg_state_msg_t wrong = { .state = KH_REG_DONE, .name_rev = 1, .name = "Eve" };
    memcpy(wrong.token, other, sizeof(wrong.token));
    feed(KH_K_REG_STATE, 0, p, kh_reg_state_encode(&wrong, p, sizeof(p)), 1300);
    CHECK_EQ(h.reg_state, KH_REG_OPENED);
    m.state = KH_REG_DONE;
    m.name_rev = 2;
    strcpy(m.name, "\xE5\xB0\x8F\xE6\x98\x8E");
    feed(KH_K_REG_STATE, 0, p, kh_reg_state_encode(&m, p, sizeof(p)), 1400);
    CHECK_EQ(h.reg_state, KH_REG_DONE);
    CHECK(kj_hubc_take_my_name_changed(&h));
    CHECK(strcmp(h.my_name, "\xE5\xB0\x8F\xE6\x98\x8E") == 0);
    kj_hubc_reg_stop(&h);
    CHECK_EQ(count_kind(out, tick(5000, out), KH_K_REG), 0);
}

int main(void)
{
    test_discovery();
    test_my_name();
    test_names();
    test_registration();
    KJ_TEST_DONE("test_kj_hubc");
}
