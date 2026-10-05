// main/kj_dns.c —— 极简 DNS 应答（纯 C）。
#include "kj_dns.h"

#include <stdbool.h>
#include <string.h>

#define DNS_HDR     12
#define T_A         1
#define T_ANY       255
#define C_IN        1
#define F_QR        0x8000
#define F_AA        0x0400
#define F_RD        0x0100
#define F_RA        0x0080

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

size_t kj_dns_reply(const uint8_t *q, size_t n, uint32_t ip, uint8_t *out, size_t cap)
{
    if (!q || !out || n < DNS_HDR || n > KJ_DNS_MAX) return 0;
    uint16_t flags = rd16(q + 2);
    if ((flags & F_QR) || ((flags >> 11) & 0xF) != 0) return 0;   // 只答标准查询
    if (rd16(q + 4) != 1) return 0;                               // 只处理一个问题
    // 问题段：标签序列（不允许压缩指针）+ 类型 + 类
    size_t i = DNS_HDR, name_len = 0;
    for (;;) {
        if (i >= n) return 0;
        uint8_t len = q[i];
        if (len == 0) {
            i++;
            break;
        }
        if (len & 0xC0) return 0;
        name_len += (size_t)len + 1;
        if (name_len > 255 || i + 1 + len > n) return 0;
        i += 1 + (size_t)len;
    }
    if (i + 4 > n) return 0;
    uint16_t qtype = rd16(q + i), qclass = rd16(q + i + 2);
    size_t qend = i + 4;
    bool answer = (qtype == T_A || qtype == T_ANY) && qclass == C_IN;
    size_t total = qend + (answer ? 16 : 0);
    if (total > cap || total > KJ_DNS_MAX) return 0;
    memcpy(out, q, qend);
    wr16(out + 2, (uint16_t)(F_QR | F_AA | (flags & F_RD) | F_RA));   // NOERROR
    wr16(out + 4, 1);
    wr16(out + 6, answer ? 1 : 0);
    wr16(out + 8, 0);
    wr16(out + 10, 0);
    if (answer) {
        uint8_t *a = out + qend;
        wr16(a, 0xC00C);              // 名字：指回问题段
        wr16(a + 2, T_A);
        wr16(a + 4, C_IN);
        a[6] = 0;                     // TTL 60 s
        a[7] = 0;
        a[8] = 0;
        a[9] = 60;
        wr16(a + 10, 4);
        memcpy(a + 12, &ip, 4);
    }
    return total;
}
