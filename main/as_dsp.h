// main/as_dsp.h —— 声景合成用的整数定点 DSP 积木（纯 C，主机可测）。
//
// ESP32-C3 没有硬件浮点，运行期一律用整数：样本为 Q15（±32767），系数为 Q15，
// 慢变化的包络 / 衰减用 Q30。乘法统一走 int64，避免中间结果溢出。
// 只有启动时的正弦表初始化用到 <math.h>。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AS_SAMPLE_RATE 16000
#define AS_Q15 32768
#define AS_Q30 (1 << 30)

// ---- 随机数（xorshift32，确定性，便于测试复现） ----

typedef struct {
    uint32_t s;
} as_rng_t;

void as_rng_seed(as_rng_t *r, uint32_t seed);
uint32_t as_rng_next(as_rng_t *r);
// 均匀白噪声样本，范围 [-32768, 32767]。
int32_t as_rng_s16(as_rng_t *r);
// [lo, hi] 闭区间均匀整数。
int32_t as_rng_range(as_rng_t *r, int32_t lo, int32_t hi);
// 以 num/den 的概率返回真。
bool as_rng_chance(as_rng_t *r, uint32_t num, uint32_t den);

// ---- 饱和 ----

static inline int32_t as_clamp16(int32_t x) {
    return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
}

static inline int32_t as_mul_q15(int32_t x, int32_t g) {
    return (int32_t)(((int64_t)x * g) >> 15);
}

// ---- 一阶低通 / 高通 ----
// 状态额外保留 8 位小数，低截止频率时不会因截断卡死在死区。

typedef struct {
    int32_t a;      // Q15 系数
    int32_t y;      // 输出 × 256
} as_lp1_t;

// 截止频率 → 一阶低通 Q15 系数（a ≈ w/(1+w)，w = 2π·fc/fs；只用整数）。
int32_t as_lp1_coef(int32_t fc_hz);
void as_lp1_init(as_lp1_t *f, int32_t fc_hz);
int32_t as_lp1(as_lp1_t *f, int32_t x);
// 高通 = 输入 − 同截止频率的低通。
int32_t as_hp1(as_lp1_t *f, int32_t x);

// ---- 状态变量滤波器（Chamberlin），同时给出低通 / 带通 / 高通 ----

typedef struct {
    int32_t f;      // Q15，≈ 2·sin(π·fc/fs)
    int32_t q;      // Q15，阻尼 = 1/Q
    int32_t low;    // 状态（Q15 样本）
    int32_t band;
} as_svf_t;

void as_svf_init(as_svf_t *s, int32_t fc_hz, int32_t q_x10);
// 只改频率与阻尼，不清状态（用于平滑扫频）。q_x10 = Q × 10。
void as_svf_set(as_svf_t *s, int32_t fc_hz, int32_t q_x10);
// 处理一个样本；返回带通，low / high 可选输出。
int32_t as_svf(as_svf_t *s, int32_t x, int32_t *low, int32_t *high);

// ---- 噪声颜色 ----

typedef struct {
    int32_t b0, b1, b2;
} as_pink_t;

void as_pink_init(as_pink_t *p);
// 输入白噪声，输出 −3 dB/倍频程的粉噪声（Paul Kellet 经济型三极点近似）。
int32_t as_pink(as_pink_t *p, int32_t white);

// ---- 正弦振荡 ----

#define AS_SINE_BITS 10
#define AS_SINE_LEN (1 << AS_SINE_BITS)

// 初始化正弦表（启动时调用一次，幂等）。
void as_sine_init(void);
// 相位为 32 位定点一周；返回 Q15 正弦（线性插值）。
int32_t as_sine(uint32_t phase);
// 频率（Hz × 16）→ 每样本相位增量。
uint32_t as_phase_inc_x16(int32_t hz_x16);

// ---- 指数衰减 ----

// 时间常数 tau_ms → 每样本乘法因子（Q30）：env *= k 后约 tau 时间衰减到 1/e。
int32_t as_decay_coef(int32_t tau_ms);
static inline int32_t as_decay(int32_t env_q30, int32_t k_q30) {
    return (int32_t)(((int64_t)env_q30 * k_q30) >> 30);
}

// ---- 慢速随机漂移（平滑地走向随机目标，用于阵风、雨势等） ----

typedef struct {
    int32_t value;     // Q15 [0, 32767]
    int32_t target;
    int32_t step;      // 每次更新的最大变化量
    int32_t lo, hi;    // 目标范围
    uint32_t hold;     // 到达目标后再停留的更新次数
} as_drift_t;

void as_drift_init(as_drift_t *d, int32_t lo, int32_t hi, int32_t step, int32_t start);
// 每次调用前进一步（通常每 16 个样本调用一次），返回当前值。
int32_t as_drift(as_drift_t *d, as_rng_t *r);
