// main/as_mixer.h —— 三层声景混音器（纯 C 定点，主机可测）。
//
// 增益链：每层声部 × 层音量 → 求和 → × 播放淡入淡出 × 睡眠定时渐弱 → 120 Hz 高通 → 软限幅。
// 所有增益变化都按块内逐样本线性插值，不会出现台阶式的"咔哒"声：
//   - 播放 / 暂停：约 1 s 淡入淡出；
//   - 层音量：约 300 ms 平滑过渡；
//   - 换声音：旧声部 300 ms 淡出、新声部同时淡入（交叉淡化）；交叉淡化进行中再次换声音，
//     新请求排队到淡化结束再执行，不会硬切仍在发声的声部。
// 主音量不在这里：由 codec 音量控制（见 as_cfg_volume_percent），保留数字域的动态范围。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "as_dsp.h"
#include "as_sounds.h"

#define AS_LAYERS 3
#define AS_LEVEL_MAX 10

typedef struct {
    as_voice_t cur, old;       // 当前声部、正在淡出的旧声部
    uint8_t sound;             // 当前声音（AS_SOUND_NONE = 空）
    uint8_t level;             // 层音量 0..AS_LEVEL_MAX
    uint8_t pending_sound;     // 交叉淡化中收到的换声请求（AS_SOUND_NONE 也是合法请求）
    bool pending;
    bool old_active;
    int32_t gain;              // Q15 当前声部增益
    int32_t old_gain;          // Q15 旧声部增益（只降不升）
    uint8_t meter;             // 本层最近一块的电平 0..255（界面动画用）
} as_layer_t;

typedef struct {
    as_layer_t layers[AS_LAYERS];
    int32_t transport;         // Q15 播放淡入淡出
    int32_t transport_target;
    int32_t fade;              // Q15 睡眠定时渐弱
    int32_t fade_target;
    as_lp1_t hp;               // 去掉小喇叭放不出的超低频，留出余量
    uint32_t seed;
    uint8_t meter;             // 混音输出电平 0..255
} as_mixer_t;

void as_mixer_init(as_mixer_t *m);

// 设置某层的声音与音量。声音不变只改音量时平滑过渡；声音改变时交叉淡化。
void as_mixer_set_layer(as_mixer_t *m, int idx, uint8_t sound, uint8_t level);
void as_mixer_set_playing(as_mixer_t *m, bool playing);
// 睡眠定时渐弱增益（Q15，32768 = 不衰减）。
void as_mixer_set_fade(as_mixer_t *m, int32_t gain_q15);

// 已完全静音且没有任何过渡在进行：音频任务可以停止写 I2S。
bool as_mixer_silent(const as_mixer_t *m);

// 渲染 n 个样本（n ≤ AS_MIXER_MAX_BLOCK）。
#define AS_MIXER_MAX_BLOCK 256
void as_mixer_render(as_mixer_t *m, int16_t *out, size_t n);

// 层音量（0..10）→ Q15 增益：约 −26 dB 到 0 dB 的近似对数曲线，0 档静音。
int32_t as_level_gain(uint8_t level);
// 软限幅：±20000 以内线性，超出部分平滑压向 ±32767。
int32_t as_soft_limit(int32_t x);
