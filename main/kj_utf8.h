// main/kj_utf8.h —— 昵称用的 UTF-8 小工具：校验、按字符边界截断、显示宽度、清理。纯 C，
// 主机测试见 tests/test_kj_utf8.c。只接受标准 UTF-8（拒绝超长编码、代理区、超过 U+10FFFF）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// s 的前 n 个字节是否是完整、合法的 UTF-8。
bool kj_utf8_valid(const char *s, size_t n);
// 解码一个字符：成功返回字节数（1..4）并写入 *cp，非法返回 0。
int kj_utf8_decode(const char *s, size_t n, uint32_t *cp);
// 显示宽度：ASCII 记 1，其余字符记 2（屏幕上汉字约是英文字母的两倍宽）。非法序列按字节计 1。
int kj_utf8_units(const char *s);
// 把 in 的前 n 个字节清理后写入 out（含结尾 0）：丢掉非法序列与控制字符，
// 在不超过 cap - 1 字节的字符边界处截断。返回写入的字节数（不含结尾 0）。
size_t kj_utf8_sanitize(const char *in, size_t n, char *out, size_t cap);
