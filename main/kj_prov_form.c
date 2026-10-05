// main/kj_prov_form.c —— 配网表单解析与扫描结果 JSON（纯 C）。
#include "kj_prov_form.h"
#include "kj_utf8.h"

#include <stdio.h>
#include <string.h>

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// URL 解码到 out（cap 含结尾 0）；超长或 %XX 不合法返回 -1，否则返回长度。
static int url_decode(const char *s, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= n) return -1;   // % 后面不足两位
            int hi = hexval(s[i + 1]);
            int lo = hexval(s[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            c = (char)(hi * 16 + lo);
            i += 2;
        }
        if (o + 1 >= cap) return -1;
        out[o++] = c;
    }
    out[o] = '\0';
    return (int)o;
}

bool kj_prov_parse_ip(const char *s, uint32_t *ip)
{
    unsigned v[4];
    char tail;
    if (!s || sscanf(s, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4) return false;
    uint8_t b[4];
    for (int i = 0; i < 4; i++) {
        if (v[i] > 255) return false;
        b[i] = (uint8_t)v[i];
    }
    if (b[0] == 0 || b[0] == 127 || b[0] >= 224) return false;   // 0.x / 回环 / 组播 / 保留
    memcpy(ip, b, 4);
    return true;
}

static bool all_hex(const char *s)
{
    for (; *s; s++) {
        if (hexval(*s) < 0) return false;
    }
    return true;
}

bool kj_prov_form_parse(const char *body, size_t n, kj_prov_form_t *out, const char **err)
{
    const char *dummy;
    if (!err) err = &dummy;
    memset(out, 0, sizeof(*out));
    *err = "form";
    if (!body || n == 0 || n > KJ_PROV_FORM_MAX) return false;
    bool have_ssid = false;
    char hub[24] = "";
    size_t i = 0;
    while (i < n) {
        size_t end = i;
        while (end < n && body[end] != '&') end++;
        const char *eq = memchr(body + i, '=', end - i);
        if (eq) {
            size_t klen = (size_t)(eq - (body + i));
            const char *val = eq + 1;
            size_t vlen = (size_t)(body + end - val);
            if (klen == 4 && memcmp(body + i, "ssid", 4) == 0) {
                *err = "ssid";
                int len = url_decode(val, vlen, out->ssid, sizeof(out->ssid));
                if (len < 1 || !kj_utf8_valid(out->ssid, (size_t)len)) return false;
                have_ssid = true;
            } else if (klen == 4 && memcmp(body + i, "pass", 4) == 0) {
                *err = "pass";
                if (url_decode(val, vlen, out->pass, sizeof(out->pass)) < 0) return false;
            } else if (klen == 3 && memcmp(body + i, "hub", 3) == 0) {
                *err = "hub";
                if (url_decode(val, vlen, hub, sizeof(hub)) < 0) return false;
            }
        }
        i = end + 1;
    }
    if (!have_ssid) {
        *err = "ssid";
        return false;
    }
    size_t plen = strlen(out->pass);
    // 开放网络没有密码；WPA 口令 8~63 个字符，或者 64 位十六进制的 PSK
    if (plen != 0 && (plen < 8 || plen > 64 || (plen == 64 && !all_hex(out->pass)))) {
        *err = "pass";
        return false;
    }
    if (hub[0] && !kj_prov_parse_ip(hub, &out->hub_ip)) {
        *err = "hub";
        return false;
    }
    *err = NULL;
    return true;
}

// 往 out 追加 SSID 的 JSON 字符串内容；放不下返回 false。
static bool put_escaped(char *out, size_t cap, size_t *o, const char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        int k = kj_utf8_decode(s + i, n - i, &cp);
        char tmp[8];
        const char *piece;
        size_t plen;
        if (k == 0) {   // 非法字节
            piece = "\\ufffd";
            plen = 6;
            k = 1;
        } else if (cp < 0x20 || cp == '"' || cp == '\\' || cp == '<' || cp == '>' || cp == '&' || cp == 0x7F) {
            snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)cp);
            piece = tmp;
            plen = 6;
        } else {
            piece = s + i;
            plen = (size_t)k;
        }
        if (*o + plen >= cap) return false;
        memcpy(out + *o, piece, plen);
        *o += plen;
        i += (size_t)k;
    }
    return true;
}

size_t kj_prov_scan_json(const kj_ap_rec_t *aps, int n, char *out, size_t cap)
{
    if (cap < 3) {
        if (cap) out[0] = '\0';
        return 0;
    }
    // 选出要列的条目：非隐藏、同名取最强，按信号从强到弱（最多 32 个）。
    int order[32];
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (!aps[i].ssid[0]) continue;
        int dup = -1;
        for (int k = 0; k < count; k++) {
            if (strcmp(aps[order[k]].ssid, aps[i].ssid) == 0) dup = k;
        }
        if (dup >= 0) {
            if (aps[i].rssi > aps[order[dup]].rssi) order[dup] = i;
            continue;
        }
        if (count < 32) order[count++] = i;
    }
    for (int a = 1; a < count; a++) {
        int v = order[a], b = a;
        while (b > 0 && aps[order[b - 1]].rssi < aps[v].rssi) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = v;
    }
    size_t o = 0;
    out[o++] = '[';
    bool first = true;
    for (int k = 0; k < count; k++) {
        const kj_ap_rec_t *ap = &aps[order[k]];
        size_t mark = o;
        char head[8], tail[32];
        snprintf(head, sizeof(head), "%s{\"s\":\"", first ? "" : ",");
        snprintf(tail, sizeof(tail), "\",\"r\":%d,\"l\":%d}", ap->rssi, ap->secure ? 1 : 0);
        bool ok = o + strlen(head) < cap;
        if (ok) {
            memcpy(out + o, head, strlen(head));
            o += strlen(head);
            ok = put_escaped(out, cap, &o, ap->ssid);
        }
        if (ok && o + strlen(tail) + 1 < cap) {   // 给结尾的 ] 留位置
            memcpy(out + o, tail, strlen(tail));
            o += strlen(tail);
            first = false;
        } else {
            o = mark;   // 放不下：整条丢掉
            break;
        }
    }
    out[o++] = ']';
    out[o] = '\0';
    return o;
}
