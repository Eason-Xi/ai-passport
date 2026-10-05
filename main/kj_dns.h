// main/kj_dns.h —— 配网热点用的极简 DNS 应答（弹窗认证）：任何 A 记录查询都答成设备自己的地址，
// 其他类型回空应答。只处理单个问题的标准查询；畸形包、应答包不回。纯 C，主机测试见 tests/test_kj_dns.c。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define KJ_DNS_MAX 512

// q / n：收到的 UDP 载荷；ip：要答的 IPv4（网络字节序，内存里依次是 a.b.c.d）。
// 返回写入 out 的长度；不该回复时返回 0。
size_t kj_dns_reply(const uint8_t *q, size_t n, uint32_t ip, uint8_t *out, size_t cap);
