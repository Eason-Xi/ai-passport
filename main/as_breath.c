// main/as_breath.c —— 呼吸节奏与渐弱曲线，说明见 as_breath.h。
#include "as_breath.h"

#include "as_cfg.h"

// 每种节奏四个阶段的秒数（0 表示没有该阶段）。
static const uint8_t PATTERN_S[AS_BREATH_COUNT][4] = {
    [AS_BREATH_478] = { 4, 7, 8, 0 },
    [AS_BREATH_EVEN] = { 5, 0, 5, 0 },
    [AS_BREATH_BOX] = { 4, 4, 4, 4 },
};

static const uint8_t *pattern_of(uint8_t pattern) {
    return PATTERN_S[pattern < AS_BREATH_COUNT ? pattern : 0];
}

uint32_t as_breath_cycle_ms(uint8_t pattern) {
    const uint8_t *s = pattern_of(pattern);
    return 1000u * (uint32_t)(s[0] + s[1] + s[2] + s[3]);
}

// smoothstep：u²(3 − 2u)，Q15。
static int32_t ease(int32_t u) {
    const int64_t u2 = (int64_t)u * u >> 15;
    return (int32_t)(u2 * (3 * 32768 - 2 * u) >> 15);
}

void as_breath_at(uint8_t pattern, uint32_t elapsed_ms, as_breath_t *out) {
    const uint8_t *s = pattern_of(pattern);
    const uint32_t cycle = as_breath_cycle_ms(pattern);
    out->cycle = elapsed_ms / cycle;
    uint32_t t = elapsed_ms % cycle;
    for (uint8_t ph = 0; ph < 4; ph++) {
        const uint32_t len = 1000u * s[ph];
        if (len == 0) continue;
        if (t < len) {
            out->phase = ph;
            out->remain_s = (uint8_t)((len - t + 999u) / 1000u);
            const int32_t u = (int32_t)((uint64_t)t * 32767u / len);
            switch (ph) {
            case AS_PHASE_INHALE: out->size = ease(u); break;
            case AS_PHASE_HOLD_IN: out->size = 32767; break;
            case AS_PHASE_EXHALE: out->size = 32767 - ease(u); break;
            default: out->size = 0; break;
            }
            return;
        }
        t -= len;
    }
    // 不会到达（t < cycle）；保守地给出起点状态。
    out->phase = AS_PHASE_INHALE;
    out->remain_s = s[0];
    out->size = 0;
}

int32_t as_timer_fade_gain(uint32_t remaining_ms) {
    if (remaining_ms >= AS_FADE_MS) return 32768;
    const int32_t u = (int32_t)((uint64_t)remaining_ms * 32768u / AS_FADE_MS);
    return (int32_t)((int64_t)u * u >> 15);
}
