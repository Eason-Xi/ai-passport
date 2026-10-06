// main/as_fonts.c —— 字形覆盖自检（固件启动时与主机预览中都会运行）。
#include "as_fonts.h"

#include "as_font_glyphs.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "as_fonts";
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

int as_fonts_selfcheck(void) {
    int missing = 0;
    missing += check(&as_zh14, "as_zh14", AS_GLYPHS_TEXT, AS_GLYPHS_TEXT_COUNT);
    missing += check(&as_zh18, "as_zh18", AS_GLYPHS_TEXT, AS_GLYPHS_TEXT_COUNT);
    missing += check(&as_zh28, "as_zh28", AS_GLYPHS_TEXT, AS_GLYPHS_TEXT_COUNT);
    missing += check(&as_num48, "as_num48", AS_GLYPHS_BIG, AS_GLYPHS_BIG_COUNT);
    if (missing == 0) {
        REPORT_OK("字形自检通过：正文 %d 个码点 × 3 个字号，大号数字 %d 个码点", AS_GLYPHS_TEXT_COUNT,
                  AS_GLYPHS_BIG_COUNT);
    }
    return missing;
}
