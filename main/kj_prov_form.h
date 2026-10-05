// main/kj_prov_form.h —— 配网网页的输入处理：表单解析与校验、扫描结果转 JSON（防 XSS 转义）。
// 纯 C，主机测试见 tests/test_kj_prov_form.c。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KJ_PROV_FORM_MAX 256   // 表单正文上限（ssid + pass + hub，URL 编码后）

typedef struct {
    char ssid[33];
    char pass[65];
    uint32_t hub_ip;   // 可选的电脑地址（IPv4，网络字节序）；0 = 自动寻找
} kj_prov_form_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t secure;    // 需要密码
} kj_ap_rec_t;

// 解析 application/x-www-form-urlencoded 正文（字段 ssid、pass、hub）。
// 失败返回 false，*err 为出错字段："ssid" / "pass" / "hub" / "form"。
bool kj_prov_form_parse(const char *body, size_t n, kj_prov_form_t *out, const char **err);
// "a.b.c.d" → IPv4（网络字节序）；不合法返回 false。
bool kj_prov_parse_ip(const char *s, uint32_t *ip);
// 扫描结果 → JSON 数组 [{"s":"…","r":-60,"l":1},…]：去掉隐藏网络、同名只留信号最强的、按信号排序；
// SSID 里的引号、反斜杠、控制字符、尖括号和 & 都转义成 \uXXXX，非法 UTF-8 换成 �。
// 放不下的条目整条丢掉。返回长度（不含结尾 0）。
size_t kj_prov_scan_json(const kj_ap_rec_t *aps, int n, char *out, size_t cap);
