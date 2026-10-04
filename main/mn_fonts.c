// main/mn_fonts.c —— 字形覆盖自检（固件启动时与主机预览中都会运行）。
#include "mn_fonts.h"

#include "mn_font_glyphs.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "mn_fonts";
#define REPORT(...) ESP_LOGE(TAG, __VA_ARGS__)
#define REPORT_OK(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#include <stdio.h>
#define REPORT(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#define REPORT_OK(...) ((void)0)
#endif

// 逐码点查询 font，找不到或只得到占位框都算缺失。
static int check(const lv_font_t *font, const char *name, const uint32_t *points, int count) {
    int missing = 0;
    for (int i = 0; i < count; i++) {
        lv_font_glyph_dsc_t dsc;
        const bool found = lv_font_get_glyph_dsc(font, &dsc, points[i], 0);
        if (!found || dsc.is_placeholder) {
            REPORT("%s 缺字形 U+%04X", name, (unsigned)points[i]);
            missing++;
        }
    }
    return missing;
}

int mn_fonts_selfcheck(void) {
    int missing = 0;
    missing += check(&mn_zh14, "mn_zh14", MN_GLYPHS_TEXT, MN_GLYPHS_TEXT_COUNT);
    missing += check(&mn_zh18, "mn_zh18", MN_GLYPHS_TEXT, MN_GLYPHS_TEXT_COUNT);
    missing += check(&mn_num88, "mn_num88", MN_GLYPHS_BIG, MN_GLYPHS_BIG_COUNT);
    if (missing == 0) {
        REPORT_OK("字形自检通过：正文 %d 个码点 × 2 个字号，大号数字 %d 个码点",
                  MN_GLYPHS_TEXT_COUNT, MN_GLYPHS_BIG_COUNT);
    }
    return missing;
}
