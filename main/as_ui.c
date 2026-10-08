// main/as_ui.c —— 界面骨架：screen、公共小工具、页面调度、"晚安"与低电量覆盖层。
#include "as_ui.h"

#include <stdio.h>

#include "as_fonts.h"
#include "as_strings.h"
#include "as_theme.h"
#include "as_ui_internal.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_root;          // 当前页面的根容器（页面切换时清空重建）
static lv_obj_t *s_night;         // "晚安"覆盖层
static lv_obj_t *s_lowbatt;       // 低电量覆盖层（lv_layer_top）
static bool s_audio_ok = true;
static uint8_t s_page = 0xFF;
static int s_soc = -1;

// ---- 公共小工具 ----

lv_obj_t *as_ui_box(lv_obj_t *parent, int x, int y, int w, int h, int32_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    if (color >= 0) {
        lv_obj_set_style_bg_color(o, lv_color_hex((uint32_t)color), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

lv_obj_t *as_ui_circle(lv_obj_t *parent, int cx, int cy, int d, int32_t color) {
    lv_obj_t *o = as_ui_box(parent, cx - d / 2, cy - d / 2, d, d, color, LV_RADIUS_CIRCLE);
    return o;
}

void as_ui_circle_move(lv_obj_t *o, int cx, int cy, int d) {
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_size(o, d, d);
}

lv_obj_t *as_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *as_ui_label_center(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int x,
                             int y, int w) {
    lv_obj_t *l = as_ui_label(parent, font, color, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

void as_ui_moon(lv_obj_t *parent, int x, int y, int d) {
    as_ui_box(parent, x, y, d, d, AS_C_MOON, LV_RADIUS_CIRCLE);
    // 用背景色的圆从右上方"咬掉"一块，得到月牙。背景是竖直渐变，取咬口中心高度处的颜色，
    // 否则咬口会显出一个颜色略深的圆。（只用于整屏渐变背景上，parent 坐标即屏幕坐标。）
    const int bite = d * 4 / 5;
    const int by = y - d / 8;
    lv_obj_t *o = as_ui_box(parent, x + d * 2 / 5, by, bite, bite, 0, LV_RADIUS_CIRCLE);
    const int t = (by + bite / 2) * 255 / AS_SCREEN_H;
    lv_obj_set_style_bg_color(o, lv_color_mix(lv_color_hex(AS_C_BG_BOTTOM), lv_color_hex(AS_C_BG_TOP),
                                              (uint8_t)(t < 0 ? 0 : (t > 255 ? 255 : t))), 0);
}

lv_obj_t *as_ui_segments(lv_obj_t *parent, int x, int y, int seg_w, int h, int gap) {
    lv_obj_t *bar = as_ui_box(parent, x, y, AS_LEVEL_MAX * (seg_w + gap) - gap, h, -1, 0);
    for (int i = 0; i < AS_LEVEL_MAX; i++) as_ui_box(bar, i * (seg_w + gap), 0, seg_w, h, AS_C_TRACK, 2);
    return bar;
}

void as_ui_segments_set(lv_obj_t *bar, int value, uint32_t color) {
    for (int i = 0; i < AS_LEVEL_MAX; i++) {
        lv_obj_t *seg = lv_obj_get_child(bar, i);
        lv_obj_set_style_bg_color(seg, lv_color_hex(i < value ? color : AS_C_TRACK), 0);
    }
}

const char *as_ui_sound_name(uint8_t sound) {
    return sound < AS_SOUND_COUNT ? AS_STR_SOUND[sound] : AS_STR_SOUND_NONE;
}

// ---- 覆盖层 ----

// 全屏夜空底色的覆盖层，中间一枚月亮与两行文字。
static lv_obj_t *notice_create(lv_obj_t *parent, const char *title, const char *sub, bool moon) {
    lv_obj_t *o = as_ui_box(parent, 0, 0, AS_SCREEN_W, AS_SCREEN_H, AS_C_BG_TOP, 0);
    lv_obj_set_style_bg_grad_color(o, lv_color_hex(AS_C_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    if (moon) {
        as_ui_moon(o, 84, 70, 72);
        // 几颗星星：大小不一的小点。
        static const int16_t STARS[][3] = { { 40, 60, 3 }, { 196, 92, 2 }, { 172, 40, 3 }, { 58, 150, 2 },
                                            { 200, 168, 3 } };
        for (size_t i = 0; i < sizeof STARS / sizeof STARS[0]; i++) {
            as_ui_circle(o, STARS[i][0], STARS[i][1], STARS[i][2], AS_C_TEXT_DIM);
        }
    } else {
        as_ui_circle(o, 120, 110, 64, AS_C_DANGER);
        as_ui_label_center(o, AS_FONT_TITLE, AS_C_MOON_DEEP, "!", 88, 92, 64);
    }
    as_ui_label_center(o, AS_FONT_TITLE, moon ? AS_C_MOON : AS_C_DANGER, title, 0, 186, AS_SCREEN_W);
    as_ui_label_center(o, AS_FONT_SMALL, AS_C_TEXT_DIM, sub, 0, 230, AS_SCREEN_W);
    return o;
}

void as_ui_lowbatt(bool show) {
    if (show && !s_lowbatt) {
        s_lowbatt = notice_create(lv_layer_top(), AS_STR_LOWBATT, AS_STR_LOWBATT_SUB, false);
    } else if (!show && s_lowbatt) {
        lv_obj_delete(s_lowbatt);
        s_lowbatt = NULL;
    }
}

// ---- 页面调度 ----

// 页面控件指针全部作废（页面被清空或 screen 被替换之前调用）。
static void forget_all(void) {
    as_ui_home_forget();
    as_ui_pages_forget();
    s_night = NULL;
}

void as_ui_init(bool audio_ok) {
    forget_all();
    s_page = 0xFF;
    s_audio_ok = audio_ok;
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scr);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(AS_C_BG_TOP), 0);
    lv_obj_set_style_bg_grad_color(s_scr, lv_color_hex(AS_C_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(s_scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    s_root = as_ui_box(s_scr, 0, 0, AS_SCREEN_W, AS_SCREEN_H, -1, 0);
    lv_screen_load(s_scr);
}

void as_ui_show(const as_model_t *m, uint32_t now_ms) {
    forget_all();
    lv_obj_clean(s_root);
    s_page = m->page;
    switch (m->page) {
    case AS_PAGE_HOME:
        as_ui_home_build(s_root, m, s_audio_ok);
        break;
    case AS_PAGE_MENU:
        as_ui_home_build(s_root, m, s_audio_ok);
        as_ui_menu_build(s_root, m);
        break;
    case AS_PAGE_MIXER: as_ui_mixer_build(s_root, m); break;
    case AS_PAGE_PICKER: as_ui_picker_build(s_root, m); break;
    case AS_PAGE_TIMER: as_ui_timer_build(s_root, m); break;
    case AS_PAGE_BREATH: as_ui_breath_build(s_root, m); break;
    default: break;
    }
    if (m->night) s_night = notice_create(s_root, AS_STR_NIGHT, AS_STR_NIGHT_SUB, true);
    as_ui_home_set_battery(s_soc);
    as_ui_update(m, now_ms);
}

void as_ui_update(const as_model_t *m, uint32_t now_ms) {
    if (m->page != s_page) {
        as_ui_show(m, now_ms);
        return;
    }
    switch (m->page) {
    case AS_PAGE_HOME: as_ui_home_update(m, now_ms, s_audio_ok); break;
    case AS_PAGE_MENU:
        as_ui_home_update(m, now_ms, s_audio_ok);
        as_ui_menu_update(m);
        break;
    case AS_PAGE_MIXER: as_ui_mixer_update(m); break;
    case AS_PAGE_PICKER: as_ui_picker_update(m); break;
    case AS_PAGE_TIMER: as_ui_timer_update(m); break;
    case AS_PAGE_BREATH: as_ui_breath_update(m); break;
    default: break;
    }
}

void as_ui_set_battery(int soc) {
    s_soc = soc;
    as_ui_home_set_battery(soc);
}

void as_ui_animate(uint32_t now_ms, const uint8_t meters[4], bool screen_on) {
    if (!screen_on || s_night) return;
    if (s_page == AS_PAGE_HOME || s_page == AS_PAGE_MENU) as_ui_home_animate(now_ms, meters);
    else if (s_page == AS_PAGE_BREATH) as_ui_breath_animate(now_ms);
}
