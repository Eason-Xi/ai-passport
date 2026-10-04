// main/mn_layout.c —— 界面几何计算，说明见 mn_layout.h。
#include "mn_layout.h"

#include <math.h>

uint8_t mn_layout_dots(uint8_t beats, mn_dot_t *out) {
    if (beats < 1) beats = 1;
    if (beats > 12) beats = 12;
    const int width = MN_DOTS_X1 - MN_DOTS_X0;
    // 直径取能放下的最大值，间隔不小于 MN_DOT_GAP_MIN，再整体居中。
    int d = (width - (beats - 1) * MN_DOT_GAP_MIN) / beats;
    if (d > MN_DOT_MAX) d = MN_DOT_MAX;
    int gap = beats > 1 ? (width - beats * d) / (beats - 1) : 0;
    if (gap > d) gap = d;   // 点少时不要散得太开
    const int used = beats * d + (beats - 1) * gap;
    const int left = MN_DOTS_X0 + (width - used) / 2;
    for (uint8_t i = 0; i < beats; i++) {
        out[i].x = (int16_t)(left + i * (d + gap) + d / 2);
        out[i].d = (uint8_t)d;
    }
    return beats;
}

int16_t mn_pendulum_angle(uint32_t beat_no, uint32_t frac_q16, int16_t amp) {
    if (frac_q16 > 65535u) frac_q16 = 65535u;
    // 偶数拍头在左（-amp），奇数拍头在右（+amp）。
    const float phase = (float)frac_q16 / 65536.0f;
    const float c = cosf(3.14159265f * phase);
    const float side = (beat_no & 1u) ? 1.0f : -1.0f;
    return (int16_t)lrintf(side * c * (float)amp);
}
