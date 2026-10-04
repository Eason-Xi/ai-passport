// main/mn_layout.h —— 界面几何计算（纯逻辑）：节拍点排布与摆杆角度。
#pragma once

#include <stdint.h>

#define MN_SCREEN_W 240
#define MN_SCREEN_H 320
#define MN_SCREEN_RADIUS 30     // 与 BSP_LVGL_SCREEN_RADIUS 一致：圆角外的像素被强制涂黑
#define MN_DOTS_X0 22           // 节拍点行的可用水平范围 [X0, X1)
#define MN_DOTS_X1 218
#define MN_DOT_MAX 20           // 节拍点最大直径
#define MN_DOT_GAP_MIN 4

typedef struct {
    int16_t x;      // 圆心 x
    uint8_t d;      // 直径
} mn_dot_t;

// 计算 beats（1–12）个节拍点在一行内居中排布的位置；out 至少 12 项。返回实际点数。
uint8_t mn_layout_dots(uint8_t beats, mn_dot_t *out);

// 摆杆角度（0.1° 为单位，正值向右）：beat_no 为已经过的拍数，frac_q16 为当前拍内进度
// （0..65535）。每个拍头摆到一侧极点（偶数拍在左、奇数拍在右），拍中经过竖直位置，
// 按余弦运动，与真实机械节拍器相同。amp 为最大摆角（0.1°）。
int16_t mn_pendulum_angle(uint32_t beat_no, uint32_t frac_q16, int16_t amp);
