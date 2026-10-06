// main/as_ui_internal.h —— 界面模块之间共享的小工具与页面入口（不对应用公开）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "as_model.h"
#include "lvgl.h"

// 无主题的基础矩形：去掉全部默认样式，color < 0 表示透明。
lv_obj_t *as_ui_box(lv_obj_t *parent, int x, int y, int w, int h, int32_t color, int radius);
// 圆（以圆心定位）。
lv_obj_t *as_ui_circle(lv_obj_t *parent, int cx, int cy, int d, int32_t color);
void as_ui_circle_move(lv_obj_t *o, int cx, int cy, int d);
lv_obj_t *as_ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
// 宽度固定、文字居中的标签（x 为左边缘）。
lv_obj_t *as_ui_label_center(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text, int x,
                             int y, int w);
// 月牙图标：直径 d，左上角 (x, y)（屏幕坐标，画在整屏渐变背景上）。
void as_ui_moon(lv_obj_t *parent, int x, int y, int d);
// 10 格音量条（左上角 (x, y)，每格 seg_w 宽）。
lv_obj_t *as_ui_segments(lv_obj_t *parent, int x, int y, int seg_w, int h, int gap);
void as_ui_segments_set(lv_obj_t *bar, int value, uint32_t color);
const char *as_ui_sound_name(uint8_t sound);

// 页面构建与刷新（root 为页面根容器）。
void as_ui_home_build(lv_obj_t *root, const as_model_t *m, bool audio_ok);
void as_ui_home_update(const as_model_t *m, uint32_t now_ms, bool audio_ok);
void as_ui_home_animate(uint32_t now_ms, const uint8_t meters[4]);
void as_ui_home_set_battery(int soc);
void as_ui_home_forget(void);

void as_ui_menu_build(lv_obj_t *root, const as_model_t *m);
void as_ui_menu_update(const as_model_t *m);

void as_ui_mixer_build(lv_obj_t *root, const as_model_t *m);
void as_ui_mixer_update(const as_model_t *m);
void as_ui_picker_build(lv_obj_t *root, const as_model_t *m);
void as_ui_picker_update(const as_model_t *m);
void as_ui_timer_build(lv_obj_t *root, const as_model_t *m);
void as_ui_timer_update(const as_model_t *m);
void as_ui_breath_build(lv_obj_t *root, const as_model_t *m);
void as_ui_breath_update(const as_model_t *m);
void as_ui_breath_animate(uint32_t now_ms);
void as_ui_pages_forget(void);
