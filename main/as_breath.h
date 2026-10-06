// main/as_breath.h —— 呼吸引导节奏与睡眠定时渐弱曲线（纯 C，主机可测）。
#pragma once

#include <stdint.h>

typedef enum {
    AS_PHASE_INHALE = 0,   // 吸气：圆从小到大
    AS_PHASE_HOLD_IN,      // 屏息（吸满）：保持大
    AS_PHASE_EXHALE,       // 呼气：从大到小
    AS_PHASE_HOLD_OUT,     // 屏息（呼尽）：保持小
} as_breath_phase_t;

typedef struct {
    uint8_t phase;         // as_breath_phase_t
    uint8_t remain_s;      // 本阶段剩余整秒（向上取整，至少 1）
    int32_t size;          // Q15，0 = 最小，32767 = 最大（已做缓入缓出）
    uint32_t cycle;        // 已完成的完整循环数
} as_breath_t;

// 节奏一个完整循环的时长（毫秒）。
uint32_t as_breath_cycle_ms(uint8_t pattern);
// 从开始引导起经过 elapsed_ms 时的状态。
void as_breath_at(uint8_t pattern, uint32_t elapsed_ms, as_breath_t *out);

// 睡眠定时最后 AS_FADE_MS 毫秒的渐弱增益（Q15）：剩余时间按平方曲线下降，
// 听感上接近匀速变轻；剩余 ≥ AS_FADE_MS 时为 32768，到 0 时为 0。
#define AS_FADE_MS 60000u
int32_t as_timer_fade_gain(uint32_t remaining_ms);
