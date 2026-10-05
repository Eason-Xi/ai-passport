// main/kj_utf8.c —— UTF-8 小工具（纯 C）。
#include "kj_utf8.h"

int kj_utf8_decode(const char *s, size_t n, uint32_t *cp)
{
    if (n == 0) return 0;
    const unsigned char *p = (const unsigned char *)s;
    uint32_t c = p[0];
    int len;
    uint32_t min;
    if (c < 0x80) {
        *cp = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        len = 2;
        min = 0x80;
        c &= 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        min = 0x800;
        c &= 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        min = 0x10000;
        c &= 0x07;
    } else {
        return 0;
    }
    if ((size_t)len > n) return 0;
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (p[i] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    *cp = c;
    return len;
}

bool kj_utf8_valid(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        int k = kj_utf8_decode(s + i, n - i, &cp);
        if (k == 0) return false;
        i += (size_t)k;
    }
    return true;
}

int kj_utf8_units(const char *s)
{
    int units = 0;
    size_t n = 0;
    while (s[n]) n++;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        int k = kj_utf8_decode(s + i, n - i, &cp);
        if (k == 0) {
            units++;
            i++;
        } else {
            units += cp < 0x80 ? 1 : 2;
            i += (size_t)k;
        }
    }
    return units;
}

size_t kj_utf8_sanitize(const char *in, size_t n, char *out, size_t cap)
{
    if (cap == 0) return 0;
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        int k = kj_utf8_decode(in + i, n - i, &cp);
        if (k == 0) {   // 非法字节：跳过
            i++;
            continue;
        }
        bool control = cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0);
        if (!control) {
            if (o + (size_t)k > cap - 1) break;   // 放不下整个字符就停在字符边界
            for (int j = 0; j < k; j++) out[o++] = in[i + (size_t)j];
        }
        i += (size_t)k;
    }
    out[o] = '\0';
    return o;
}
