// main/mn_tap.c —— 敲击测速，说明见 mn_tap.h。
#include "mn_tap.h"

#include <string.h>

#include "mn_sched.h"

void mn_tap_reset(mn_tap_t *t) {
    memset(t, 0, sizeof *t);
}

static uint64_t interval_sum(const mn_tap_t *t) {
    uint64_t sum = 0;
    for (uint8_t i = 0; i < t->n_iv; i++) sum += t->iv[i];
    return sum;
}

static void push_interval(mn_tap_t *t, uint32_t dt) {
    t->iv[t->head] = dt;
    t->head = (uint8_t)((t->head + 1) % MN_TAP_HISTORY);
    if (t->n_iv < MN_TAP_HISTORY) t->n_iv++;
}

bool mn_tap_add(mn_tap_t *t, int64_t now_us) {
    if (t->taps == 0) {
        t->taps = 1;
        t->last_us = now_us;
        return true;
    }
    const int64_t dt = now_us - t->last_us;
    if (dt < MN_TAP_MIN_US) return false;
    if (dt > MN_TAP_RESET_US) {
        mn_tap_reset(t);
        t->taps = 1;
        t->last_us = now_us;
        return true;
    }
    if (t->n_iv >= 2) {
        const uint64_t mean = interval_sum(t) / t->n_iv;
        const uint64_t diff = (uint64_t)dt > mean ? (uint64_t)dt - mean : mean - (uint64_t)dt;
        if (diff * 100u > mean * 35u) {
            // 换了速度：上一次敲击作为新序列的第 1 下，本次是第 2 下。
            t->n_iv = 0;
            t->head = 0;
            t->taps = 1;
        }
    }
    push_interval(t, (uint32_t)dt);
    if (t->taps < 255) t->taps++;
    t->last_us = now_us;
    return true;
}

uint16_t mn_tap_bpm(const mn_tap_t *t) {
    if (t->n_iv == 0) return 0;
    const uint64_t sum = interval_sum(t);
    // BPM = 60e6 × n / sum，四舍五入。
    uint64_t bpm = (60000000ull * t->n_iv + sum / 2) / sum;
    if (bpm < MN_BPM_MIN) bpm = MN_BPM_MIN;
    if (bpm > MN_BPM_MAX) bpm = MN_BPM_MAX;
    return (uint16_t)bpm;
}

uint8_t mn_tap_count(const mn_tap_t *t) {
    return t->taps;
}
