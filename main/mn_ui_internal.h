// main/mn_ui_internal.h —— 界面模块内部共享的控件构造函数与页面接口（不对外）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "mn_beat.h"
#include "mn_model.h"

// ---- 通用构造（mn_ui.c） ----

// 无样式、不可滚动、不可点击的矩形容器。color < 0 表示透明。
lv_obj_t *mn_ui_box(lv_obj_t *parent, int x, int y, int w, int h, int32_t color, int radius);
// 指定字体与颜色的标签。
lv_obj_t *mn_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
// 颜色线性混合：t = 0..255，0 为 a，255 为 b。
lv_color_t mn_ui_mix(uint32_t a, uint32_t b, int t);

// ---- 主界面（mn_ui_main.c） ----

void mn_ui_main_create(lv_obj_t *scr);
void mn_ui_main_update(const mn_model_t *m);
void mn_ui_main_beat(const mn_beat_t *b);
void mn_ui_main_stop(void);
// 开始倒数：在 BPM 位置显示剩余秒数（琥珀色）；第一拍到来或停止时自动恢复。
void mn_ui_main_count(uint8_t remaining);
// 摆杆角度（0.1°）与闪光强度 0..255。
void mn_ui_main_pendulum(int16_t angle, int flash, bool accent);

// ---- 设置面板（mn_ui_settings.c） ----

void mn_ui_settings_open(lv_obj_t *scr, bool animate);
void mn_ui_settings_close(void);
bool mn_ui_settings_is_open(void);
void mn_ui_settings_update(const mn_model_t *m);
void mn_ui_settings_led(int flash, bool accent);

// ---- 敲击测速页（mn_ui_tap.c） ----

void mn_ui_tap_open(lv_obj_t *scr);
void mn_ui_tap_close(void);
bool mn_ui_tap_is_open(void);
void mn_ui_tap_update(const mn_model_t *m);
void mn_ui_tap_ring(int flash);
