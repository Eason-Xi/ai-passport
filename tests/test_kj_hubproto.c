// tests/test_kj_hubproto.c —— 设备 ⇄ hub 线协议：信封与各控制消息往返、畸形输入拒收、名字清理 / 截断、
// token 字符集，以及与 Python 一侧共用的黄金向量（tests/data/kj_hub_vectors.txt）。
// 用 `test_kj_hubproto --print` 打印当前编码，便于核对新增的向量。
#include "kj_hubproto.h"
#include "kj_test.h"

#include <stdio.h>
#include <string.h>

static const uint8_t SRC[6] = { 0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC };
static const uint8_t GAME_FRAME[6] = { 'K', 'J', 2, 1, 0xF2, 0xA3 };

// 生成一个命名向量的完整数据报；未知名称返回 0。
static size_t build(const char *name, uint8_t *out, size_t cap)
{
    uint8_t payload[KH_PAYLOAD_MAX];
    size_t plen = 0;
    kh_env_t e = { .room = 0xA3F2, .seq = 0x0102, .rssi = -60 };
    memcpy(e.src, SRC, 6);
    memcpy(e.dst, KH_MAC_HUB, 6);
    if (strcmp(name, "frame") == 0) {
        e.kind = KH_K_FRAME;
        e.flags = KH_FLAG_HOST;
        memcpy(e.dst, KH_MAC_BROADCAST, 6);
        memcpy(payload, GAME_FRAME, sizeof(GAME_FRAME));
        plen = sizeof(GAME_FRAME);
    } else if (strcmp(name, "discover") == 0) {
        kh_discover_t d = { .hub_proto = KH_VERSION, .game_proto = 2, .role = KH_ROLE_PLAYER, .boot = 0xBEEF,
                            .fw = "1.1.0", .name_rev = 3, .name = "\xE5\xB0\x8F\xE6\x98\x8E" };   // 小明
        e.kind = KH_K_DISCOVER;
        plen = kh_discover_encode(&d, payload, sizeof(payload));
    } else if (strcmp(name, "offer") == 0) {
        kh_offer_t o = { .hub_id = 0x12345678, .http_port = KH_PORT_HTTP, .tcp_port = KH_PORT_TCP, .roster_rev = 7,
                         .flags = KH_OFFER_NAME_VALID, .name_rev = 3, .name = "\xE5\xB0\x8F\xE6\x98\x8E" };
        e.kind = KH_K_OFFER;
        memcpy(e.src, KH_MAC_HUB, 6);
        memcpy(e.dst, SRC, 6);
        plen = kh_offer_encode(&o, payload, sizeof(payload));
    } else if (strcmp(name, "reg") == 0) {
        e.kind = KH_K_REG;
        plen = kh_reg_encode("ABCDEFG2", payload, sizeof(payload));
    } else if (strcmp(name, "reg_state") == 0) {
        kh_reg_state_msg_t m = { .token = "ABCDEFG2", .state = KH_REG_DONE, .name_rev = 4, .name = "Amy" };
        e.kind = KH_K_REG_STATE;
        memcpy(e.src, KH_MAC_HUB, 6);
        memcpy(e.dst, SRC, 6);
        plen = kh_reg_state_encode(&m, payload, sizeof(payload));
    } else if (strcmp(name, "name_get") == 0) {
        const uint8_t nos[] = { 3, 7, 128 };
        e.kind = KH_K_NAME_GET;
        plen = kh_name_get_encode(nos, 3, payload, sizeof(payload));
    } else if (strcmp(name, "names") == 0) {
        kh_names_t m = { .roster_rev = 9, .count = 2 };
        m.e[0].no = 3;
        strcpy(m.e[0].name, "\xE5\xB0\x8F\xE6\x98\x8E");
        m.e[1].no = 7;
        m.e[1].flags = KH_NAME_BOT | KH_NAME_UNKNOWN;
        e.kind = KH_K_NAMES;
        memcpy(e.src, KH_MAC_HUB, 6);
        memcpy(e.dst, SRC, 6);
        plen = kh_names_encode(&m, payload, sizeof(payload));
    } else {
        return 0;
    }
    if (plen == 0) return 0;
    e.payload = payload;
    e.len = (uint16_t)plen;
    return kh_env_encode(&e, out, cap);
}

static const char *const NAMES[] = { "frame", "discover", "offer", "reg", "reg_state", "name_get", "names" };

static void hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", p[i]);
    out[2 * n] = '\0';
}

static void test_vectors(void)
{
    FILE *f = fopen("tests/data/kj_hub_vectors.txt", "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[2 * KH_DGRAM_MAX + 64];
    int checked = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char name[32], want[2 * KH_DGRAM_MAX + 1];
        if (sscanf(line, "%31s %1008s", name, want) != 2) continue;
        uint8_t buf[KH_DGRAM_MAX];
        size_t n = build(name, buf, sizeof(buf));
        char got[2 * KH_DGRAM_MAX + 1];
        hex(buf, n, got);
        if (n == 0 || strcmp(got, want) != 0) {
            fprintf(stderr, "vector %s mismatch:\n  want %s\n  got  %s\n", name, want, got);
            kj_test_failures++;
        }
        kh_env_t e;
        CHECK(kh_env_decode(buf, n, &e));
        checked++;
    }
    fclose(f);
    CHECK_EQ(checked, (int)(sizeof(NAMES) / sizeof(NAMES[0])));
}

static void test_envelope(void)
{
    uint8_t buf[KH_DGRAM_MAX];
    size_t n = build("frame", buf, sizeof(buf));
    CHECK_EQ(n, KH_HDR + 6);
    kh_env_t e;
    CHECK(kh_env_decode(buf, n, &e));
    CHECK_EQ(e.kind, KH_K_FRAME);
    CHECK(memcmp(e.src, SRC, 6) == 0);
    CHECK(memcmp(e.dst, KH_MAC_BROADCAST, 6) == 0);
    CHECK_EQ(e.room, 0xA3F2);
    CHECK_EQ(e.seq, 0x0102);
    CHECK_EQ(e.rssi, -60);
    CHECK_EQ(e.flags, KH_FLAG_HOST);
    CHECK_EQ(e.len, 6);
    CHECK(memcmp(e.payload, GAME_FRAME, 6) == 0);
    // 截断 / 多余字节 / 魔数 / 版本 / kind / 长度字段不符
    for (size_t cut = 0; cut < n; cut++) CHECK(!kh_env_decode(buf, cut, &e));
    uint8_t bad[KH_DGRAM_MAX];
    memcpy(bad, buf, n);
    bad[n] = 0;
    CHECK(!kh_env_decode(bad, n + 1, &e));
    memcpy(bad, buf, n);
    bad[1] = 'J';
    CHECK(!kh_env_decode(bad, n, &e));
    memcpy(bad, buf, n);
    bad[2] = KH_VERSION + 1;
    CHECK(!kh_env_decode(bad, n, &e));
    memcpy(bad, buf, n);
    bad[3] = KH_K_COUNT;
    CHECK(!kh_env_decode(bad, n, &e));
    memcpy(bad, buf, n);
    bad[3] = 0;
    CHECK(!kh_env_decode(bad, n, &e));
    // 载荷上限
    static uint8_t big[KH_PAYLOAD_MAX + 1];
    kh_env_t ee = { .kind = KH_K_FRAME, .payload = big, .len = KH_PAYLOAD_MAX };
    static uint8_t out[KH_DGRAM_MAX + 8];
    CHECK_EQ(kh_env_encode(&ee, out, sizeof(out)), KH_DGRAM_MAX);
    ee.len = KH_PAYLOAD_MAX + 1;
    CHECK_EQ(kh_env_encode(&ee, out, sizeof(out)), 0);
    ee.len = 10;
    CHECK_EQ(kh_env_encode(&ee, out, 20), 0);   // 缓冲区不足
    CHECK(KH_DGRAM_MAX <= 1472);                // 一个以太网 MTU 内（设备侧不重组 IP 分片）
}

static void test_messages(void)
{
    uint8_t buf[KH_DGRAM_MAX];
    kh_env_t e;
    // DISCOVER
    size_t n = build("discover", buf, sizeof(buf));
    CHECK(kh_env_decode(buf, n, &e));
    kh_discover_t d;
    CHECK(kh_discover_decode(e.payload, e.len, &d));
    CHECK_EQ(d.role, KH_ROLE_PLAYER);
    CHECK_EQ(d.boot, 0xBEEF);
    CHECK(strcmp(d.fw, "1.1.0") == 0);
    CHECK(strcmp(d.name, "\xE5\xB0\x8F\xE6\x98\x8E") == 0);
    CHECK(!kh_discover_decode(e.payload, e.len - 1, &d));   // 截断
    // OFFER
    n = build("offer", buf, sizeof(buf));
    CHECK(kh_env_decode(buf, n, &e));
    kh_offer_t o;
    CHECK(kh_offer_decode(e.payload, e.len, &o));
    CHECK_EQ(o.hub_id, 0x12345678);
    CHECK_EQ(o.http_port, KH_PORT_HTTP);
    CHECK_EQ(o.tcp_port, KH_PORT_TCP);
    CHECK_EQ(o.roster_rev, 7);
    CHECK_EQ(o.flags, KH_OFFER_NAME_VALID);
    CHECK(strcmp(o.name, "\xE5\xB0\x8F\xE6\x98\x8E") == 0);
    // REG / REG_STATE
    char token[KJ_REG_TOKEN_LEN + 1];
    n = build("reg", buf, sizeof(buf));
    CHECK(kh_env_decode(buf, n, &e));
    CHECK(kh_reg_decode(e.payload, e.len, token));
    CHECK(strcmp(token, "ABCDEFG2") == 0);
    CHECK_EQ(kh_reg_encode("abcdefg2", buf, sizeof(buf)), 0);   // 小写不合法
    CHECK_EQ(kh_reg_encode("ABCDEFG1", buf, sizeof(buf)), 0);   // 1 不在字母表
    CHECK(kh_token_valid("ZZ234567"));
    n = build("reg_state", buf, sizeof(buf));
    CHECK(kh_env_decode(buf, n, &e));
    kh_reg_state_msg_t rs;
    CHECK(kh_reg_state_decode(e.payload, e.len, &rs));
    CHECK_EQ(rs.state, KH_REG_DONE);
    CHECK_EQ(rs.name_rev, 4);
    CHECK(strcmp(rs.name, "Amy") == 0);
    // NAME_GET / NAMES
    n = build("name_get", buf, sizeof(buf));
    CHECK(kh_env_decode(buf, n, &e));
    uint8_t nos[KH_NAMES_MAX];
    CHECK_EQ(kh_name_get_decode(e.payload, e.len, nos), 3);
    CHECK_EQ(nos[2], 128);
    uint8_t seven[8] = { 7, 1, 2, 3, 4, 5, 6, 7 };
    CHECK_EQ(kh_name_get_decode(seven, sizeof(seven), nos), -1);   // 超过 6 个
    uint8_t zero[2] = { 1, 0 };
    CHECK_EQ(kh_name_get_decode(zero, sizeof(zero), nos), -1);     // 编号 0
    CHECK_EQ(kh_name_get_encode(nos, 0, buf, sizeof(buf)), 0);
    n = build("names", buf, sizeof(buf));
    CHECK(kh_env_decode(buf, n, &e));
    kh_names_t nm;
    CHECK(kh_names_decode(e.payload, e.len, &nm));
    CHECK_EQ(nm.roster_rev, 9);
    CHECK_EQ(nm.count, 2);
    CHECK(strcmp(nm.e[0].name, "\xE5\xB0\x8F\xE6\x98\x8E") == 0);
    CHECK_EQ(nm.e[1].flags, KH_NAME_BOT | KH_NAME_UNKNOWN);
    CHECK(!kh_names_decode(e.payload, e.len - 1, &nm));
}

static void test_name_cleanup(void)
{
    uint8_t p[64];
    kh_offer_t o = { .http_port = 1, .tcp_port = 2 }, back;
    // 超长：按字符边界截断到 24 字节（8 个汉字 = 24 字节；第 9 个放不下）
    strcpy(o.name, "");
    const char *long_name = "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE5\x9B\x9B\xE4\xBA\x94\xE5\x85\xAD"
                            "\xE4\xB8\x83\xE5\x85\xAB";   // 一二三四五六七八（24 字节）
    memcpy(o.name, long_name, 24);
    o.name[24] = '\0';
    size_t n = kh_offer_encode(&o, p, sizeof(p));
    CHECK(kh_offer_decode(p, n, &back));
    CHECK_EQ(strlen(back.name), 24);
    // 控制字符与非法字节在编码时被清理
    strcpy(o.name, "A\x01" "B\xFF" "C");
    n = kh_offer_encode(&o, p, sizeof(p));
    CHECK(kh_offer_decode(p, n, &back));
    CHECK(strcmp(back.name, "ABC") == 0);
    // 解码不信任对端：名字长度字段超过上限 → 拒收
    uint8_t raw[64];
    memset(raw, 0, sizeof(raw));
    raw[4] = 1;                                   // http_port = 1
    raw[6] = 2;                                   // tcp_port = 2
    size_t at = 4 + 2 + 2 + 4 + 1 + 4;            // hub_id、端口、roster_rev、flags、name_rev 之后是名字长度
    raw[at] = KJ_NAME_MAX + 1;
    CHECK(!kh_offer_decode(raw, at + 1 + KJ_NAME_MAX + 1, &back));
    // 对端发来非法 UTF-8：解码时清理
    raw[at] = 3;
    raw[at + 1] = 'x';
    raw[at + 2] = 0xC3;                           // 两字节序列缺了后半
    raw[at + 3] = 'y';
    CHECK(kh_offer_decode(raw, at + 4, &back));
    CHECK(strcmp(back.name, "xy") == 0);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--print") == 0) {
        for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
            uint8_t buf[KH_DGRAM_MAX];
            char h[2 * KH_DGRAM_MAX + 1];
            hex(buf, build(NAMES[i], buf, sizeof(buf)), h);
            printf("%s %s\n", NAMES[i], h);
        }
        return 0;
    }
    test_envelope();
    test_messages();
    test_name_cleanup();
    test_vectors();
    KJ_TEST_DONE("test_kj_hubproto");
}
