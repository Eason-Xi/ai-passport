// tests/test_kj_prov_form.c —— 配网表单：URL 解码、各字段长度边界、可选的电脑地址、错误字段；
// 扫描结果 JSON：去重、排序、隐藏网络、转义（引号 / 反斜杠 / 控制字符 / 尖括号 / 非法 UTF-8）、容量不足。
#include "kj_prov_form.h"
#include "kj_test.h"

#include <string.h>

static bool parse(const char *body, kj_prov_form_t *f, const char **err)
{
    return kj_prov_form_parse(body, strlen(body), f, err);
}

static void test_form(void)
{
    kj_prov_form_t f;
    const char *err = NULL;
    CHECK(parse("ssid=Home+WiFi&pass=12345678", &f, &err));
    CHECK(strcmp(f.ssid, "Home WiFi") == 0);
    CHECK(strcmp(f.pass, "12345678") == 0);
    CHECK_EQ(f.hub_ip, 0);
    CHECK(err == NULL);
    // %XX（含中文 UTF-8）、字段顺序无关、未知字段忽略
    CHECK(parse("x=1&pass=a%26b%3Dc%25d123&ssid=%E5%AE%B6", &f, &err));
    CHECK(strcmp(f.ssid, "\xE5\xAE\xB6") == 0);
    CHECK(strcmp(f.pass, "a&b=c%d123") == 0);
    // 开放网络：空密码
    CHECK(parse("ssid=Cafe&pass=", &f, &err));
    CHECK_EQ(f.pass[0], '\0');
    CHECK(parse("ssid=Cafe", &f, &err));
    // 可选的电脑地址
    CHECK(parse("ssid=A&pass=12345678&hub=192.168.1.20", &f, &err));
    uint8_t b[4];
    memcpy(b, &f.hub_ip, 4);
    CHECK(b[0] == 192 && b[1] == 168 && b[2] == 1 && b[3] == 20);
    CHECK(parse("ssid=A&hub=", &f, &err));   // 空 = 自动寻找
    CHECK_EQ(f.hub_ip, 0);

    // 错误：缺 ssid / ssid 太长 / 密码太短太长 / 64 位但不是十六进制 / 地址不合法 / 编码不合法 / 正文太长
    CHECK(!parse("pass=12345678", &f, &err));
    CHECK(strcmp(err, "ssid") == 0);
    CHECK(!parse("ssid=", &f, &err));
    CHECK(strcmp(err, "ssid") == 0);
    CHECK(!parse("ssid=123456789012345678901234567890123", &f, &err));   // 33 字节
    CHECK(parse("ssid=12345678901234567890123456789012", &f, &err));     // 32 字节
    CHECK(!parse("ssid=A&pass=1234567", &f, &err));
    CHECK(strcmp(err, "pass") == 0);
    CHECK(parse("ssid=A&pass=123456789012345678901234567890123456789012345678901234567890123", &f, &err));
    char hex64[96] = "ssid=A&pass=";
    for (int i = 0; i < 64; i++) strcat(hex64, "a");
    CHECK(parse(hex64, &f, &err));
    hex64[strlen(hex64) - 1] = 'z';
    CHECK(!parse(hex64, &f, &err));
    CHECK(!parse("ssid=A&hub=300.1.1.1", &f, &err));
    CHECK(strcmp(err, "hub") == 0);
    CHECK(!parse("ssid=A&hub=127.0.0.1", &f, &err));
    CHECK(!parse("ssid=A&hub=1.2.3", &f, &err));
    CHECK(!parse("ssid=A&hub=1.2.3.4x", &f, &err));
    CHECK(!parse("ssid=%E5%AE", &f, &err));     // 截断的 UTF-8
    CHECK(!parse("ssid=%ZZ", &f, &err));
    CHECK(!parse("ssid=A%2", &f, &err));
    char big[KJ_PROV_FORM_MAX + 8];
    memset(big, 'a', sizeof(big) - 1);
    memcpy(big, "ssid=", 5);
    big[sizeof(big) - 1] = '\0';
    CHECK(!parse(big, &f, &err));
    CHECK(strcmp(err, "form") == 0);
    CHECK(!kj_prov_form_parse(NULL, 3, &f, &err));
}

static void test_scan_json(void)
{
    kj_ap_rec_t aps[] = {
        { "Weak", -80, 1 },
        { "", -30, 1 },                     // 隐藏网络
        { "Home", -50, 1 },
        { "Home", -40, 1 },                 // 同名：取更强的
        { "Open \"Cafe\"", -60, 0 },
        { "a\\b<c>&\x01", -70, 1 },
        { "bad\xFF", -90, 0 },
    };
    char out[512];
    size_t n = kj_prov_scan_json(aps, (int)(sizeof(aps) / sizeof(aps[0])), out, sizeof(out));
    CHECK_EQ(n, strlen(out));
    CHECK(strcmp(out,
                 "[{\"s\":\"Home\",\"r\":-40,\"l\":1},"
                 "{\"s\":\"Open \\u0022Cafe\\u0022\",\"r\":-60,\"l\":0},"
                 "{\"s\":\"a\\u005cb\\u003cc\\u003e\\u0026\\u0001\",\"r\":-70,\"l\":1},"
                 "{\"s\":\"Weak\",\"r\":-80,\"l\":1},"
                 "{\"s\":\"bad\\ufffd\",\"r\":-90,\"l\":0}]") == 0);
    // 容量不足：只保留放得下的完整条目
    char small[40];
    n = kj_prov_scan_json(aps, 7, small, sizeof(small));
    CHECK(strcmp(small, "[{\"s\":\"Home\",\"r\":-40,\"l\":1}]") == 0);
    CHECK_EQ(n, strlen(small));
    char tiny[8];
    CHECK_EQ(kj_prov_scan_json(aps, 7, tiny, sizeof(tiny)), 2);
    CHECK(strcmp(tiny, "[]") == 0);
    CHECK_EQ(kj_prov_scan_json(aps, 0, out, sizeof(out)), 2);
}

int main(void)
{
    test_form();
    test_scan_json();
    KJ_TEST_DONE("test_kj_prov_form");
}
