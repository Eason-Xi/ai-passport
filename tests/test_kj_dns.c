// tests/test_kj_dns.c —— 配网热点的 DNS 应答：A / AAAA / ANY 查询、回显 ID 与 RD、畸形包与应答包不回、长度上限。
#include "kj_dns.h"
#include "kj_test.h"

#include <string.h>

// 构造一个标准查询：id、RD、qname（点分）、qtype；extra 为附加在末尾的字节（例如 EDNS OPT）。
static size_t query(uint8_t *buf, uint16_t id, const char *name, uint16_t qtype, int extra)
{
    size_t n = 0;
    buf[n++] = (uint8_t)(id >> 8);
    buf[n++] = (uint8_t)id;
    buf[n++] = 0x01;   // RD
    buf[n++] = 0x00;
    buf[n++] = 0;
    buf[n++] = 1;      // qdcount
    memset(buf + n, 0, 6);
    if (extra) buf[n + 5] = 1;   // arcount
    n += 6;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        buf[n++] = (uint8_t)len;
        memcpy(buf + n, p, len);
        n += len;
        p += len + (dot ? 1 : 0);
    }
    buf[n++] = 0;
    buf[n++] = (uint8_t)(qtype >> 8);
    buf[n++] = (uint8_t)qtype;
    buf[n++] = 0;
    buf[n++] = 1;      // IN
    for (int i = 0; i < extra; i++) buf[n++] = 0xEE;
    return n;
}

int main(void)
{
    const uint8_t ipb[4] = { 192, 168, 4, 1 };
    uint32_t ip;
    memcpy(&ip, ipb, 4);
    uint8_t q[600], r[600];

    // A 查询：答 192.168.4.1，回显 ID 与 RD
    size_t n = query(q, 0xBEEF, "connectivitycheck.gstatic.com", 1, 0);
    size_t m = kj_dns_reply(q, n, ip, r, sizeof(r));
    CHECK_EQ(m, n + 16);
    CHECK_EQ(r[0], 0xBE);
    CHECK_EQ(r[1], 0xEF);
    CHECK(r[2] & 0x80);            // QR
    CHECK(r[2] & 0x01);            // RD 回显
    CHECK_EQ(r[3] & 0x0F, 0);      // NOERROR
    CHECK_EQ(r[7], 1);             // ancount
    CHECK(memcmp(r + 12, q + 12, n - 12) == 0);   // 问题段原样
    CHECK_EQ(r[n], 0xC0);
    CHECK_EQ(r[n + 1], 0x0C);
    CHECK(memcmp(r + n + 12, ipb, 4) == 0);

    // AAAA：空应答（让手机退回 IPv4）
    n = query(q, 7, "captive.apple.com", 28, 0);
    m = kj_dns_reply(q, n, ip, r, sizeof(r));
    CHECK_EQ(m, n);
    CHECK_EQ(r[7], 0);
    // ANY 也答 A
    n = query(q, 8, "a.b", 255, 0);
    CHECK_EQ(kj_dns_reply(q, n, ip, r, sizeof(r)), n + 16);
    // 带 EDNS 附加记录：只复制问题段，应答里不带附加记录
    n = query(q, 9, "example.com", 1, 11);
    m = kj_dns_reply(q, n, ip, r, sizeof(r));
    CHECK_EQ(m, n - 11 + 16);
    CHECK_EQ(r[11], 0);

    // 不回：应答包、非标准查询、多个问题、截断、压缩指针、超长标签、缓冲区不足
    n = query(q, 1, "x.com", 1, 0);
    q[2] |= 0x80;
    CHECK_EQ(kj_dns_reply(q, n, ip, r, sizeof(r)), 0);
    q[2] = 0x01 | (2 << 3);   // opcode = 2
    CHECK_EQ(kj_dns_reply(q, n, ip, r, sizeof(r)), 0);
    q[2] = 0x01;
    q[5] = 2;
    CHECK_EQ(kj_dns_reply(q, n, ip, r, sizeof(r)), 0);
    q[5] = 1;
    for (size_t cut = 0; cut < n; cut++) CHECK_EQ(kj_dns_reply(q, cut, ip, r, sizeof(r)), 0);
    CHECK_EQ(kj_dns_reply(q, n, ip, r, n + 15), 0);
    uint8_t ptr[20] = { 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 0x0C, 0, 1, 0, 1 };
    CHECK_EQ(kj_dns_reply(ptr, 18, ip, r, sizeof(r)), 0);
    q[12] = 64;   // 标签超过 63
    CHECK_EQ(kj_dns_reply(q, n, ip, r, sizeof(r)), 0);
    // 名字总长超过 255
    char longname[400];
    size_t k = 0;
    for (int lab = 0; lab < 5; lab++) {
        for (int j = 0; j < 60; j++) longname[k++] = 'a';
        longname[k++] = '.';
    }
    longname[k - 1] = '\0';
    n = query(q, 2, longname, 1, 0);
    CHECK_EQ(kj_dns_reply(q, n, ip, r, sizeof(r)), 0);
    // 超过 512 字节的查询直接忽略
    CHECK_EQ(kj_dns_reply(q, 513, ip, r, sizeof(r)), 0);
    CHECK_EQ(kj_dns_reply(NULL, 20, ip, r, sizeof(r)), 0);
    KJ_TEST_DONE("test_kj_dns");
}
