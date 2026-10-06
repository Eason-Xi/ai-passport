// tests/test_as_dsp.c —— 定点 DSP 积木与声景合成器的主机测试。
#include <math.h>
#include <string.h>

#include "as_dsp.h"
#include "as_sounds.h"
#include "as_test.h"

#define SECONDS(s) ((s) * AS_SAMPLE_RATE)

static void test_rng(void) {
    as_rng_t a, b;
    as_rng_seed(&a, 7);
    as_rng_seed(&b, 7);
    for (int i = 0; i < 1000; i++) CHECK(as_rng_next(&a) == as_rng_next(&b));
    as_rng_seed(&a, 0);   // 0 种子不能卡死
    CHECK(as_rng_next(&a) != 0);
    int hits = 0;
    for (int i = 0; i < 100000; i++) {
        const int32_t v = as_rng_range(&a, -3, 5);
        CHECK(v >= -3 && v <= 5);
        hits += as_rng_chance(&a, 1, 4);
    }
    CHECK_NEAR(hits, 25000, 1000);
    int64_t sum = 0;
    for (int i = 0; i < 100000; i++) sum += as_rng_s16(&a);
    CHECK_NEAR(sum / 100000, 0, 300);
}

// 正弦表与相位增量：1 kHz 在 16 kHz 下 16 个样本一个周期。
static void test_sine(void) {
    as_sine_init();
    CHECK_NEAR(as_sine(0), 0, 2);
    CHECK_NEAR(as_sine(0x40000000u), 32767, 2);
    CHECK_NEAR(as_sine(0xC0000000u), -32767, 2);
    const uint32_t inc = as_phase_inc_x16(1000 * 16);
    CHECK_NEAR(inc, 0x10000000u, 1);
    for (uint32_t k = 0; k < 256; k++) {
        const uint32_t ph = k << 24;
        CHECK_NEAR(as_sine(ph), lrint(32767.0 * sin(ph / 4294967296.0 * 2 * M_PI)), 40);
    }
}

// 一阶低通：直流增益 1、时间常数与截止频率相符；高通去直流。
static void test_lp1(void) {
    as_lp1_t f;
    as_lp1_init(&f, 100);
    int32_t y = 0;
    for (int i = 0; i < 16000; i++) y = as_lp1(&f, 10000);
    CHECK_NEAR(y, 10000, 2);
    as_lp1_init(&f, 100);
    // 100 Hz 一阶低通的时间常数 ≈ 1.6 ms ≈ 25 样本：25 个样本后到 63% 左右。
    for (int i = 0; i < 25; i++) y = as_lp1(&f, 10000);
    CHECK(y > 5600 && y < 7000);
    as_lp1_init(&f, 50);
    for (int i = 0; i < 32000; i++) y = as_hp1(&f, 12000);
    CHECK_NEAR(y, 0, 3);
    CHECK(as_lp1_coef(10) < as_lp1_coef(100) && as_lp1_coef(100) < as_lp1_coef(1000));
}

// 状态变量带通：中心频率处的增益明显高于远离中心处，且不发散。
static int32_t svf_gain(int32_t fc, int32_t probe_hz) {
    as_sine_init();
    as_svf_t s;
    as_svf_init(&s, fc, 20);
    uint32_t ph = 0;
    const uint32_t inc = as_phase_inc_x16(probe_hz * 16);
    int32_t peak = 0;
    for (int i = 0; i < 8000; i++) {
        const int32_t y = as_svf(&s, as_sine(ph) / 4, NULL, NULL);
        ph += inc;
        if (i > 4000 && abs(y) > peak) peak = abs(y);
    }
    return peak;
}

static void test_svf(void) {
    const int32_t at = svf_gain(1000, 1000), low = svf_gain(1000, 150), high = svf_gain(1000, 4000);
    CHECK(at > 3 * low);
    CHECK(at > 3 * high);
    CHECK(at < 32767 * 4);
}

static void test_decay(void) {
    // tau = 100 ms：1600 个样本后约 1/e。
    int32_t env = AS_Q30 - 1;
    const int32_t k = as_decay_coef(100);
    for (int i = 0; i < 1600; i++) env = as_decay(env, k);
    CHECK_NEAR(env / 1000, (int32_t)(AS_Q30 * 0.3679 / 1000), (int32_t)(AS_Q30 * 0.01 / 1000));
}

static void test_drift(void) {
    as_rng_t r;
    as_rng_seed(&r, 3);
    as_drift_t d;
    as_drift_init(&d, 1000, 9000, 5, 5000);
    int32_t prev = d.value;
    for (int i = 0; i < 200000; i++) {
        const int32_t v = as_drift(&d, &r);
        CHECK(v >= 1000 && v <= 9000);
        CHECK(abs(v - prev) <= 5);   // 平滑：每步变化不超过 step
        prev = v;
    }
}

// ---- 声景 ----

typedef struct {
    double rms;
    double mean;
    int clipped;
    double low_ratio;   // 200 Hz 以下能量 / 2 kHz 以上能量
} stats_t;

static stats_t measure(uint8_t id, uint32_t seed, int seconds) {
    static as_voice_t v;
    as_voice_init(&v, id, seed);
    static int16_t buf[256];
    stats_t s = { 0 };
    as_lp1_t lo, hi;
    as_lp1_init(&lo, 200);
    as_lp1_init(&hi, 2000);
    double sum2 = 0, sum = 0, el = 0, eh = 0;
    const int total = SECONDS(seconds);
    for (int done = 0; done < total; done += 256) {
        as_voice_render(&v, buf, 256);
        for (int i = 0; i < 256; i++) {
            const int32_t x = buf[i];
            sum += x;
            sum2 += (double)x * x;
            if (x >= 32767 || x <= -32768) s.clipped++;
            const double l = as_lp1(&lo, x), h = as_hp1(&hi, x);
            el += l * l;
            eh += h * h;
        }
    }
    s.rms = sqrt(sum2 / total);
    s.mean = sum / total;
    s.low_ratio = el / (eh + 1);
    return s;
}

static void test_sounds_level_and_safety(void) {
    for (uint8_t id = 0; id < AS_SOUND_COUNT; id++) {
        const stats_t s = measure(id, 1, 30);
        fprintf(stderr, "  %-9s rms=%5.0f mean=%5.0f clip=%d low/high=%.2f\n", as_sound_debug_name(id), s.rms,
                s.mean, s.clipped, s.low_ratio);
        CHECK(s.rms > 1500 && s.rms < 7000);   // 校准后约 −22 … −15 dBFS
        CHECK(fabs(s.mean) < 400);              // 无明显直流
        CHECK(s.clipped == 0);                  // 单个声部不削波
    }
}

static void test_noise_colors(void) {
    const stats_t w = measure(AS_SOUND_WHITE, 2, 10), p = measure(AS_SOUND_PINK, 2, 10),
                  b = measure(AS_SOUND_BROWN, 2, 10);
    // 白 → 粉 → 棕，低频占比依次升高。
    CHECK(w.low_ratio < p.low_ratio);
    CHECK(p.low_ratio < b.low_ratio);
    CHECK(w.low_ratio < 0.2);
}

static void test_sounds_deterministic(void) {
    static as_voice_t a, b;
    static int16_t x[4000], y[4000];
    for (uint8_t id = 0; id < AS_SOUND_COUNT; id++) {
        as_voice_init(&a, id, 9);
        as_voice_init(&b, id, 9);
        as_voice_render(&a, x, 4000);
        as_voice_render(&b, y, 4000);
        CHECK(memcmp(x, y, sizeof x) == 0);
        as_voice_init(&b, id, 10);
        as_voice_render(&b, y, 4000);
        if (id != AS_SOUND_BOWL) CHECK(memcmp(x, y, sizeof x) != 0);   // 颂钵开头 0.3 s 内只有极轻底噪
    }
    as_voice_init(&a, 200, 1);   // 越界编号退回白噪音而不是崩溃
    CHECK(a.id == AS_SOUND_WHITE);
}

// 颂钵：两次敲击之间隔 10–17 s（看包络的突增）。
static void test_bowl_strikes(void) {
    static as_voice_t v;
    as_voice_init(&v, AS_SOUND_BOWL, 5);
    static int16_t buf[160];   // 10 ms
    int32_t prev = 0;
    int last_strike = -1, strikes = 0;
    for (int ms10 = 0; ms10 < 6000; ms10++) {   // 60 s
        as_voice_render(&v, buf, 160);
        int32_t peak = 0;
        for (int i = 0; i < 160; i++) peak = abs(buf[i]) > peak ? abs(buf[i]) : peak;
        // 与最近约 1 s 的峰值包络比较：拍频造成的起伏不会被误判为敲击。
        // 起音约 12 ms，会跨两个 10 ms 窗口：2 s 内的第二次突增视为同一次敲击。
        if (peak > prev * 3 / 2 + 1500 && (last_strike < 0 || ms10 - last_strike > 200)) {
            if (last_strike >= 0) {
                const int gap = ms10 - last_strike;
                CHECK(gap >= 990 && gap <= 1710);
            }
            last_strike = ms10;
            strikes++;
        }
        prev = peak > prev * 99 / 100 ? peak : prev * 99 / 100;
    }
    CHECK(strikes >= 4 && strikes <= 7);
}

int main(void) {
    test_rng();
    test_sine();
    test_lp1();
    test_svf();
    test_decay();
    test_drift();
    test_sounds_level_and_safety();
    test_noise_colors();
    test_sounds_deterministic();
    test_bowl_strikes();
    printf("test_as_dsp: PASS\n");
    return 0;
}
