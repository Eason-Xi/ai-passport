// main/kj_fonts.c —— 字体覆盖自检：开机时把生成的码点表逐一对照实际字体（LVGL API）。
#include "kj_fonts.h"
#include "kj_font_glyphs.h"

static bool has_glyph(const lv_font_t *font, uint32_t cp)
{
    lv_font_glyph_dsc_t dsc = { 0 };
    return lv_font_get_glyph_dsc(font, &dsc, cp, 0) && !dsc.is_placeholder;
}

static int check_set(const char *name, const lv_font_t *font, const uint32_t *cps, int n,
                     kj_font_missing_cb_t cb)
{
    int missing = 0;
    for (int i = 0; i < n; i++) {
        if (!has_glyph(font, cps[i])) {
            missing++;
            if (cb) cb(name, cps[i]);
        }
    }
    return missing;
}

int kj_fonts_selfcheck(kj_font_missing_cb_t cb)
{
    int missing = 0;
    missing += check_set("kj_zh14", &kj_zh14, KJ_GLYPHS_TEXT, KJ_GLYPHS_TEXT_COUNT, cb);
    missing += check_set("kj_zh18", &kj_zh18, KJ_GLYPHS_TEXT, KJ_GLYPHS_TEXT_COUNT, cb);
    missing += check_set("kj_zh26", &kj_zh26, KJ_GLYPHS_TEXT, KJ_GLYPHS_TEXT_COUNT, cb);
    missing += check_set("kj_big48", &kj_big48, KJ_GLYPHS_BIG, KJ_GLYPHS_BIG_COUNT, cb);
    missing += check_set("kj_num56", &kj_num56, KJ_GLYPHS_NUM, KJ_GLYPHS_NUM_COUNT, cb);
    missing += check_set("kj_hand36", &kj_hand36, KJ_GLYPHS_HAND, KJ_GLYPHS_HAND_COUNT, cb);
    missing += check_set("kj_hand64", &kj_hand64, KJ_GLYPHS_HAND, KJ_GLYPHS_HAND_COUNT, cb);
    // 反例：一个肯定不在子集里的字（U+9F98 龘）必须查不到，否则自检本身失效。
    if (has_glyph(&kj_zh18, 0x9F98)) {
        missing++;
        if (cb) cb("selfcheck-negative", 0x9F98);
    }
    return missing;
}
