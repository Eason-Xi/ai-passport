// main/mn_sound.c —— click 音色合成，说明见 mn_sound.h。
#include "mn_sound.h"

#include <math.h>

#define PI_F 3.14159265f
#define ATTACK_SAMPLES 24u    // 1.5 ms
#define RELEASE_SAMPLES 48u   // 3 ms
#define MAX_PARTIALS 4

// 一个正弦分音：频率倍数（相对基频）、相对幅度、指数衰减时间常数（毫秒）。
typedef struct {
    float ratio;
    float amp;
    float tau_ms;
} partial_t;

// 一种音色的配方：三种力度各自的基频，分音表，起音噪声。
typedef struct {
    float base_hz[MN_TICK_KIND_COUNT];
    partial_t partials[MAX_PARTIALS];
    float noise_amp;      // 起音噪声相对幅度（0 = 无）
    float noise_ms;       // 噪声持续时间
} recipe_t;

static const recipe_t RECIPES[MN_SOUND_COUNT] = {
    [MN_SOUND_BEEP] = {
        // 重音 G6、普通拍 C6、细分 A5：音高区分明显，适合电子乐与练习。
        .base_hz = { 1568.0f, 1046.5f, 880.0f, 0.0f },
        .partials = { { 1.0f, 1.0f, 14.0f }, { 2.0f, 0.08f, 8.0f } },
    },
    [MN_SOUND_WOOD] = {
        // 木块：主分音快速衰减，叠加木条类非谐和分音与 1.5 ms 敲击噪声。
        .base_hz = { 1200.0f, 880.0f, 880.0f, 0.0f },
        .partials = { { 1.0f, 1.0f, 9.0f }, { 2.76f, 0.35f, 5.0f }, { 5.4f, 0.12f, 3.0f } },
        .noise_amp = 0.4f,
        .noise_ms = 1.5f,
    },
    [MN_SOUND_COWBELL] = {
        // 牛铃：540 / 800 Hz 一对主分音（重音整体升高约大三度），衰减较长。
        .base_hz = { 675.0f, 540.0f, 540.0f, 0.0f },
        .partials = { { 1.0f, 1.0f, 30.0f }, { 1.4815f, 0.8f, 30.0f }, { 3.0f, 0.15f, 12.0f },
                      { 4.444f, 0.12f, 12.0f } },
        .noise_amp = 0.15f,
        .noise_ms = 1.0f,
    },
};

static const int16_t PEAKS[MN_TICK_KIND_COUNT] = { MN_PEAK_ACCENT, MN_PEAK_BEAT, MN_PEAK_SUB, MN_PEAK_COUNT };

// 倒数提示音：高而柔和的"嘀"，与三种节拍音色都明显不同。
static const recipe_t COUNT_RECIPE = {
    .base_hz = { 2093.0f, 2093.0f, 2093.0f, 2093.0f },
    .partials = { { 1.0f, 1.0f, 22.0f }, { 2.0f, 0.12f, 10.0f } },
};

// 确定性噪声：相同输入永远得到相同波形（便于测试与复现）。
static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// 起音 / 收尾包络：首样本为 0，末样本为 0，中间为 1。
static float edge_envelope(uint32_t i) {
    float env = 1.0f;
    if (i < ATTACK_SAMPLES) env = (float)i / (float)ATTACK_SAMPLES;
    const uint32_t left = MN_CLICK_LEN - 1u - i;
    if (left < RELEASE_SAMPLES) {
        const float rel = (float)left / (float)RELEASE_SAMPLES;
        if (rel < env) env = rel;
    }
    return env;
}

void mn_sound_render(uint8_t sound, uint8_t kind, int16_t out[MN_CLICK_LEN]) {
    if (sound >= MN_SOUND_COUNT) sound = 0;
    if (kind >= MN_TICK_KIND_COUNT) kind = 0;
    const recipe_t *r = kind == MN_TICK_COUNT ? &COUNT_RECIPE : &RECIPES[sound];
    const float base = r->base_hz[kind];
    const float dt = 1.0f / (float)MN_SAMPLE_RATE;
    const uint32_t noise_len = (uint32_t)(r->noise_ms * (float)MN_SAMPLE_RATE / 1000.0f);
    uint32_t seed = 0x9E3779B9u ^ ((uint32_t)sound << 8) ^ kind;

    static float buf[MN_CLICK_LEN];   // 只在初始化阶段使用，避免占用任务栈
    float peak = 0.0f;
    for (uint32_t i = 0; i < MN_CLICK_LEN; i++) {
        const float t = (float)i * dt;
        float v = 0.0f;
        for (int p = 0; p < MAX_PARTIALS; p++) {
            const partial_t *pa = &r->partials[p];
            if (pa->amp <= 0.0f) continue;
            const float f = base * pa->ratio;
            if (f >= (float)MN_SAMPLE_RATE / 2.0f) continue;  // 超过奈奎斯特频率的分音丢弃
            v += pa->amp * expf(-t * 1000.0f / pa->tau_ms) * sinf(2.0f * PI_F * f * t);
        }
        if (i < noise_len && r->noise_amp > 0.0f) {
            const float n = (float)(int32_t)xorshift32(&seed) / 2147483648.0f;
            v += r->noise_amp * n * (1.0f - (float)i / (float)noise_len);
        }
        v *= edge_envelope(i);
        buf[i] = v;
        const float a = fabsf(v);
        if (a > peak) peak = a;
    }
    const float gain = peak > 0.0f ? (float)PEAKS[kind] / peak : 0.0f;
    for (uint32_t i = 0; i < MN_CLICK_LEN; i++) {
        out[i] = (int16_t)lrintf(buf[i] * gain);
    }
}
