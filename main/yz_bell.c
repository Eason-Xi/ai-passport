// main/yz_bell.c —— “磬”声合成，见 yz_bell.h。
#include "yz_bell.h"

#include <math.h>
#include <stdbool.h>

#define FADE_IN 128                              // 8 ms
#define FADE_OUT (YZ_BELL_RATE * 60 / 1000)      // 60 ms
#define Q30 (1 << 30)

// 基频 660 Hz，泛音比取铜磬常见的非谐比例；振幅与衰减时间各不相同，高泛音先消失。
static const float FREQ[3] = { 660.0f, 660.0f * 2.76f, 660.0f * 5.40f };
static const float AMP[3] = { 1.0f, 0.45f, 0.20f };
static const float TAU_S[3] = { 0.60f, 0.25f, 0.12f };

static int16_t s_sin[256];
static bool s_table_ready;

void yz_bell_start(yz_bell_t *bell, int16_t peak) {
    if (!s_table_ready) {
        for (int i = 0; i < 256; i++) s_sin[i] = (int16_t)lrintf(32767.0f * sinf(6.2831853f * (float)i / 256.0f));
        s_table_ready = true;
    }
    bell->n = 0;
    bell->peak = peak;
    for (int k = 0; k < 3; k++) {
        bell->phase[k] = 0;
        bell->step[k] = (uint32_t)(FREQ[k] / (float)YZ_BELL_RATE * 4294967296.0);
        bell->env[k] = (int32_t)(AMP[k] * (float)Q30);
        bell->decay[k] = (int32_t)(expf(-1.0f / (TAU_S[k] * (float)YZ_BELL_RATE)) * (float)Q30);
    }
}

size_t yz_bell_render(yz_bell_t *bell, int16_t *out, size_t cap) {
    size_t i = 0;
    for (; i < cap && bell->n < YZ_BELL_SAMPLES; i++, bell->n++) {
        int32_t acc = 0;
        for (int k = 0; k < 3; k++) {
            acc += (int32_t)(((int64_t)s_sin[bell->phase[k] >> 24] * bell->env[k]) >> 30);
            bell->phase[k] += bell->step[k];
            bell->env[k] = (int32_t)(((int64_t)bell->env[k] * bell->decay[k]) >> 30);
        }
        // 三个泛音振幅之和为 1.65，归一化到 peak。
        int32_t s = (int32_t)((int64_t)acc * bell->peak / (32767 * 165 / 100));
        if (bell->n < FADE_IN) s = s * (int32_t)bell->n / FADE_IN;
        const uint32_t left = YZ_BELL_SAMPLES - bell->n;
        if (left < FADE_OUT) s = s * (int32_t)left / FADE_OUT;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
    }
    return i;
}
