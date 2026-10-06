// main/as_dsp.c —— 定点 DSP 积木，说明见 as_dsp.h。
#include "as_dsp.h"

#include <math.h>

// ---- 随机数 ----

void as_rng_seed(as_rng_t *r, uint32_t seed) {
    r->s = seed ? seed : 0x9E3779B9u;   // xorshift 的状态不能为 0
}

uint32_t as_rng_next(as_rng_t *r) {
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

int32_t as_rng_s16(as_rng_t *r) {
    return (int32_t)(int16_t)(as_rng_next(r) >> 16);
}

int32_t as_rng_range(as_rng_t *r, int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    const uint32_t span = (uint32_t)(hi - lo) + 1u;
    return lo + (int32_t)(((uint64_t)as_rng_next(r) * span) >> 32);
}

bool as_rng_chance(as_rng_t *r, uint32_t num, uint32_t den) {
    if (den == 0) return false;
    return ((uint64_t)as_rng_next(r) * den >> 32) < num;
}

// ---- 一阶滤波 ----

int32_t as_lp1_coef(int32_t fc_hz) {
    if (fc_hz < 1) fc_hz = 1;
    // w = 2π·fc/fs（Q15）；a = w / (1 + w)，单调且总小于 1。
    const int64_t w = (int64_t)205887 * fc_hz / AS_SAMPLE_RATE;
    return (int32_t)(w * AS_Q15 / (AS_Q15 + w));
}

void as_lp1_init(as_lp1_t *f, int32_t fc_hz) {
    f->a = as_lp1_coef(fc_hz);
    f->y = 0;
}

int32_t as_lp1(as_lp1_t *f, int32_t x) {
    const int64_t diff = ((int64_t)x << 8) - f->y;
    f->y += (int32_t)((diff * f->a) >> 15);
    return f->y >> 8;
}

int32_t as_hp1(as_lp1_t *f, int32_t x) {
    return x - as_lp1(f, x);
}

// ---- 状态变量滤波器 ----

#define SVF_LIMIT (1 << 23)   // 状态上限，防止参数突变时数值失控

void as_svf_set(as_svf_t *s, int32_t fc_hz, int32_t q_x10) {
    if (fc_hz < 20) fc_hz = 20;
    if (fc_hz > 3000) fc_hz = 3000;   // Chamberlin 结构在 fs/5 以上失准
    if (q_x10 < 5) q_x10 = 5;
    // x = π·fc/fs（Q15），f = 2·sin(x) ≈ 2·(x − x³/6 + x⁵/120)。
    const int64_t x = (int64_t)102944 * fc_hz / AS_SAMPLE_RATE;
    const int64_t x3 = x * x / AS_Q15 * x / AS_Q15;
    const int64_t x5 = x3 * x / AS_Q15 * x / AS_Q15;
    s->f = (int32_t)(2 * (x - x3 / 6 + x5 / 120));
    s->q = (int32_t)(327680 / q_x10);
}

void as_svf_init(as_svf_t *s, int32_t fc_hz, int32_t q_x10) {
    s->low = 0;
    s->band = 0;
    as_svf_set(s, fc_hz, q_x10);
}

static inline int32_t limit(int32_t v) {
    return v > SVF_LIMIT ? SVF_LIMIT : (v < -SVF_LIMIT ? -SVF_LIMIT : v);
}

int32_t as_svf(as_svf_t *s, int32_t x, int32_t *low, int32_t *high) {
    s->low = limit(s->low + (int32_t)(((int64_t)s->f * s->band) >> 15));
    const int32_t h = x - s->low - (int32_t)(((int64_t)s->q * s->band) >> 15);
    s->band = limit(s->band + (int32_t)(((int64_t)s->f * h) >> 15));
    if (low) *low = s->low;
    if (high) *high = h;
    return s->band;
}

// ---- 粉噪声 ----

void as_pink_init(as_pink_t *p) {
    p->b0 = p->b1 = p->b2 = 0;
}

// 带舍入的 Q15 乘法：避免截断偏置在长时间常数的极点里累积成直流。
static inline int32_t mul_round(int32_t x, int32_t g) {
    return (int32_t)(((int64_t)x * g + (1 << 14)) >> 15);
}

int32_t as_pink(as_pink_t *p, int32_t w) {
    p->b0 = mul_round(p->b0, 32691) + mul_round(w, 3246);    // 0.99765, 0.0990460
    p->b1 = mul_round(p->b1, 31556) + mul_round(w, 9716);    // 0.96300, 0.2965164
    p->b2 = mul_round(p->b2, 18678) + mul_round(w, 34494);   // 0.57000, 1.0526913
    const int32_t sum = p->b0 + p->b1 + p->b2 + mul_round(w, 6056);   // 0.1848
    // 三极点叠加后 RMS 约为输入的 3 倍，缩回与白噪声相近的电平。
    return sum / 3;
}

// ---- 正弦 ----

static int16_t s_sine[AS_SINE_LEN + 1];
static bool s_sine_ready;

void as_sine_init(void) {
    if (s_sine_ready) return;
    for (int i = 0; i <= AS_SINE_LEN; i++) {
        s_sine[i] = (int16_t)lrint(32767.0 * sin(2.0 * 3.14159265358979323846 * i / AS_SINE_LEN));
    }
    s_sine_ready = true;
}

int32_t as_sine(uint32_t phase) {
    const uint32_t idx = phase >> (32 - AS_SINE_BITS);
    const int32_t frac = (int32_t)((phase >> (16 - AS_SINE_BITS)) & 0xFFFF);
    const int32_t a = s_sine[idx], b = s_sine[idx + 1];
    return a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
}

uint32_t as_phase_inc_x16(int32_t hz_x16) {
    if (hz_x16 < 0) hz_x16 = 0;
    return (uint32_t)(((uint64_t)hz_x16 << 28) / AS_SAMPLE_RATE);
}

// ---- 衰减 ----

int32_t as_decay_coef(int32_t tau_ms) {
    if (tau_ms < 1) tau_ms = 1;
    const int64_t n = (int64_t)tau_ms * AS_SAMPLE_RATE / 1000;
    return (int32_t)(AS_Q30 - AS_Q30 / n);
}

// ---- 慢速漂移 ----

void as_drift_init(as_drift_t *d, int32_t lo, int32_t hi, int32_t step, int32_t start) {
    d->lo = lo;
    d->hi = hi;
    d->step = step > 0 ? step : 1;
    d->value = start;
    d->target = start;
    d->hold = 0;
}

int32_t as_drift(as_drift_t *d, as_rng_t *r) {
    if (d->value < d->target) {
        d->value += d->step;
        if (d->value > d->target) d->value = d->target;
    } else if (d->value > d->target) {
        d->value -= d->step;
        if (d->value < d->target) d->value = d->target;
    } else if (d->hold) {
        d->hold--;
    } else {
        d->target = as_rng_range(r, d->lo, d->hi);
        d->hold = (uint32_t)as_rng_range(r, 50, 400);
    }
    return d->value;
}
