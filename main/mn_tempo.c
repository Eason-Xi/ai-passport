// main/mn_tempo.c —— 速度术语与 BPM 调节规则，说明见 mn_tempo.h。
#include "mn_tempo.h"

#include "mn_sched.h"

// 各术语区间的下限（含）。
static const uint16_t TERM_MIN[MN_TEMPO_COUNT] = { 0, 40, 60, 66, 76, 108, 120, 156, 176, 200 };

uint8_t mn_tempo_term(uint16_t bpm) {
    uint8_t term = 0;
    for (uint8_t i = 0; i < MN_TEMPO_COUNT; i++) {
        if (bpm >= TERM_MIN[i]) term = i;
    }
    return term;
}

uint16_t mn_bpm_clamp(int bpm) {
    if (bpm < (int)MN_BPM_MIN) return MN_BPM_MIN;
    if (bpm > (int)MN_BPM_MAX) return MN_BPM_MAX;
    return (uint16_t)bpm;
}

uint16_t mn_bpm_bump(uint16_t bpm, int dir, uint8_t step) {
    if (dir == 0) return mn_bpm_clamp(bpm);
    if (step <= 1) return mn_bpm_clamp((int)bpm + (dir > 0 ? 1 : -1));
    int next;
    if (dir > 0) {
        next = ((int)bpm / step + 1) * step;
    } else {
        next = (int)bpm % step ? ((int)bpm / step) * step : (int)bpm - step;
    }
    return mn_bpm_clamp(next);
}

uint32_t mn_repeat_delay_ms(uint32_t n) {
    if (n < 8) return 120;
    if (n < 24) return 50;
    return 90;
}

uint8_t mn_repeat_step(uint32_t n) {
    return n < 24 ? 1 : 5;
}
