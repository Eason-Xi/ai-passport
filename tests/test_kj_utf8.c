// tests/test_kj_utf8.c —— UTF-8 小工具：合法性（超长编码 / 代理区 / 越界）、显示宽度、清理与按字符边界截断。
#include "kj_test.h"
#include "kj_utf8.h"

#include <string.h>

int main(void)
{
    // 合法
    CHECK(kj_utf8_valid("", 0));
    CHECK(kj_utf8_valid("abc", 3));
    CHECK(kj_utf8_valid("\xE5\xB0\x8F\xE6\x98\x8E", 6));        // 小明
    CHECK(kj_utf8_valid("\xF0\x9F\x98\x80", 4));                // U+1F600
    CHECK(kj_utf8_valid("\xC2\xB7", 2));                        // ·
    // 非法：截断、孤立续字节、超长编码、代理区、超出 U+10FFFF
    CHECK(!kj_utf8_valid("\xE5\xB0", 2));
    CHECK(!kj_utf8_valid("\x80", 1));
    CHECK(!kj_utf8_valid("\xC0\xAF", 2));
    CHECK(!kj_utf8_valid("\xE0\x80\xAF", 3));
    CHECK(!kj_utf8_valid("\xED\xA0\x80", 3));
    CHECK(!kj_utf8_valid("\xF4\x90\x80\x80", 4));
    CHECK(!kj_utf8_valid("\xFF", 1));

    uint32_t cp = 0;
    CHECK_EQ(kj_utf8_decode("\xE5\xB0\x8F", 3, &cp), 3);
    CHECK_EQ(cp, 0x5C0F);
    CHECK_EQ(kj_utf8_decode("A", 1, &cp), 1);
    CHECK_EQ(kj_utf8_decode("", 0, &cp), 0);

    // 显示宽度：汉字 2、ASCII 1
    CHECK_EQ(kj_utf8_units("Amy"), 3);
    CHECK_EQ(kj_utf8_units("\xE5\xB0\x8F\xE6\x98\x8E"), 4);
    CHECK_EQ(kj_utf8_units("A\xE5\xB0\x8F"), 3);

    // 清理：去掉控制字符与非法字节；放不下的字符整个丢掉，不切半个
    char out[8];
    CHECK_EQ(kj_utf8_sanitize("a\tb\x01" "c", 5, out, sizeof(out)), 3);
    CHECK(strcmp(out, "abc") == 0);
    CHECK_EQ(kj_utf8_sanitize("x\xFFy\xC3", 4, out, sizeof(out)), 2);
    CHECK(strcmp(out, "xy") == 0);
    CHECK_EQ(kj_utf8_sanitize("\xE5\xB0\x8F\xE6\x98\x8E", 6, out, 6), 3);   // 第二个字放不下
    CHECK(strcmp(out, "\xE5\xB0\x8F") == 0);
    CHECK_EQ(kj_utf8_sanitize("\xC2\x85" "Z", 3, out, sizeof(out)), 1);     // C1 控制字符 U+0085
    CHECK(strcmp(out, "Z") == 0);
    CHECK_EQ(kj_utf8_sanitize("abc", 3, out, 1), 0);
    CHECK(out[0] == '\0');
    CHECK_EQ(kj_utf8_sanitize("abc", 3, out, 0), 0);
    KJ_TEST_DONE("test_kj_utf8");
}
