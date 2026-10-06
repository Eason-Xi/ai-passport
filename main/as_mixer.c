// main/as_mixer.c —— 三层混音器，说明见 as_mixer.h。
#include "as_mixer.h"

#include <string.h>

// 每样本增益步进（Q15）。
#define TRANSPORT_STEP 2   // 32768 / 2 / 16000 ≈ 1.02 s 完成淡入或淡出
#define LAYER_STEP 7       // ≈ 0.29 s
#define FADE_STEP 7
#define LIMIT_KNEE 20000
#define LIMIT_ROOM (32767 - LIMIT_KNEE)

static const int16_t LEVEL_GAIN[AS_LEVEL_MAX + 1] = {
    0, 1640, 2950, 4590, 6550, 8850, 11470, 14750, 19000, 24900, 32767,
};

int32_t as_level_gain(uint8_t level) {
    return LEVEL_GAIN[level > AS_LEVEL_MAX ? AS_LEVEL_MAX : level];
}

int32_t as_soft_limit(int32_t x) {
    const int32_t a = x < 0 ? -x : x;
    if (a <= LIMIT_KNEE) return x;
    const int32_t over = a - LIMIT_KNEE;
    const int32_t y = LIMIT_KNEE + (int32_t)((int64_t)over * LIMIT_ROOM / (over + LIMIT_ROOM));
    return x < 0 ? -y : y;
}

static int32_t approach(int32_t v, int32_t target, int32_t max_step) {
    if (v < target) return v + max_step < target ? v + max_step : target;
    if (v > target) return v - max_step > target ? v - max_step : target;
    return v;
}

static uint32_t isqrt(uint32_t x) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

static uint8_t meter_of(uint64_t sum_abs, size_t n) {
    if (n == 0) return 0;
    const uint32_t r = isqrt((uint32_t)(sum_abs / n)) * 4u;   // 平均幅度 3000 左右 ≈ 220
    return (uint8_t)(r > 255 ? 255 : r);
}

void as_mixer_init(as_mixer_t *m) {
    memset(m, 0, sizeof *m);
    for (int i = 0; i < AS_LAYERS; i++) {
        m->layers[i].sound = AS_SOUND_NONE;
        m->layers[i].pending_sound = AS_SOUND_NONE;
    }
    m->fade = m->fade_target = AS_Q15;
    m->seed = 1;
    as_lp1_init(&m->hp, 120);
}

// 立即开始一次换声：当前声部转入淡出槽，新声部从 0 淡入。
static void start_switch(as_mixer_t *m, as_layer_t *l, uint8_t sound) {
    if (l->sound != AS_SOUND_NONE && l->gain > 0) {
        l->old = l->cur;
        l->old_gain = l->gain;
        l->old_active = true;
    }
    l->sound = sound;
    l->gain = 0;
    if (sound != AS_SOUND_NONE) as_voice_init(&l->cur, sound, m->seed++);
}

void as_mixer_set_layer(as_mixer_t *m, int idx, uint8_t sound, uint8_t level) {
    if (idx < 0 || idx >= AS_LAYERS) return;
    as_layer_t *l = &m->layers[idx];
    if (sound >= AS_SOUND_COUNT) sound = AS_SOUND_NONE;
    l->level = level > AS_LEVEL_MAX ? AS_LEVEL_MAX : level;
    if (l->pending) {
        l->pending_sound = sound;      // 覆盖排队中的请求，只保留最新的
        if (sound == l->sound) l->pending = false;
        return;
    }
    if (sound == l->sound) return;
    if (l->old_active) {
        l->pending = true;
        l->pending_sound = sound;
        return;
    }
    start_switch(m, l, sound);
}

void as_mixer_set_playing(as_mixer_t *m, bool playing) {
    m->transport_target = playing ? AS_Q15 : 0;
}

void as_mixer_set_fade(as_mixer_t *m, int32_t gain_q15) {
    m->fade_target = gain_q15 < 0 ? 0 : (gain_q15 > AS_Q15 ? AS_Q15 : gain_q15);
}

bool as_mixer_silent(const as_mixer_t *m) {
    return m->transport == 0 && m->transport_target == 0;
}

// 把声部渲染后按 g0 → g1 线性插值的增益累加进 acc，返回本层贡献的绝对值之和。
static uint64_t add_voice(as_voice_t *v, int32_t g0, int32_t g1, int32_t *acc, size_t n) {
    static int16_t tmp[AS_MIXER_MAX_BLOCK];
    as_voice_render(v, tmp, n);
    const int32_t step = (int32_t)((((int64_t)g1 - g0) << 8) / (int64_t)n);
    int32_t g = g0 << 8;
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        const int32_t s = as_mul_q15(tmp[i], g >> 8);
        acc[i] += s;
        sum += (uint32_t)(s < 0 ? -s : s);
        g += step;
    }
    return sum;
}

void as_mixer_render(as_mixer_t *m, int16_t *out, size_t n) {
    if (n > AS_MIXER_MAX_BLOCK) n = AS_MIXER_MAX_BLOCK;
    if (n == 0) return;
    if (as_mixer_silent(m)) {
        memset(out, 0, n * sizeof *out);
        m->meter = 0;
        for (int i = 0; i < AS_LAYERS; i++) m->layers[i].meter = 0;
        return;
    }
    static int32_t acc[AS_MIXER_MAX_BLOCK];
    memset(acc, 0, n * sizeof *acc);
    const int32_t span = (int32_t)n;

    for (int i = 0; i < AS_LAYERS; i++) {
        as_layer_t *l = &m->layers[i];
        uint64_t sum = 0;
        if (l->old_active) {
            const int32_t g1 = approach(l->old_gain, 0, LAYER_STEP * span);
            sum += add_voice(&l->old, l->old_gain, g1, acc, n);
            l->old_gain = g1;
            if (g1 == 0) {
                l->old_active = false;
                if (l->pending) {
                    l->pending = false;
                    if (l->pending_sound != l->sound) start_switch(m, l, l->pending_sound);
                }
            }
        }
        if (l->sound != AS_SOUND_NONE) {
            const int32_t g1 = approach(l->gain, as_level_gain(l->level), LAYER_STEP * span);
            if (l->gain > 0 || g1 > 0) sum += add_voice(&l->cur, l->gain, g1, acc, n);
            l->gain = g1;
        }
        l->meter = meter_of(sum, n);
    }

    // 主增益 = 播放淡入淡出 × 定时渐弱，同样逐样本插值。
    const int32_t t1 = approach(m->transport, m->transport_target, TRANSPORT_STEP * span);
    const int32_t f1 = approach(m->fade, m->fade_target, FADE_STEP * span);
    const int32_t g0 = as_mul_q15(m->transport, m->fade), g1 = as_mul_q15(t1, f1);
    m->transport = t1;
    m->fade = f1;
    const int32_t step = (int32_t)((((int64_t)g1 - g0) << 8) / span);
    int32_t g = g0 << 8;
    uint64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        const int32_t s = as_soft_limit(as_hp1(&m->hp, as_mul_q15(acc[i], g >> 8)));
        out[i] = (int16_t)as_clamp16(s);
        sum += (uint32_t)(s < 0 ? -s : s);
        g += step;
    }
    m->meter = meter_of(sum, n);
}
