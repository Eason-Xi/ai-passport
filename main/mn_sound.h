// main/mn_sound.h —— 节拍器 click 音色合成（纯逻辑，无 ESP-IDF / LVGL 依赖）。
//
// 三种音色 × 三种力度（重音 / 普通拍 / 细分音），全部在启动时合成到内存里，
// 运行中切换音色只是换指针，音频任务不做任何计算。
// 每个 click 固定 MN_CLICK_LEN 个样本（40 ms），短于最快 250 BPM × 十六分时的 tick 间隔
// （60 ms），因此不会被下一个 click 截断。波形以 1.5 ms 线性起音开始、最后 3 ms 线性收尾到 0，
// 避免起止处的直流跳变产生"噗"声；峰值按力度归一化，保证不削波。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mn_sched.h"

#define MN_CLICK_LEN 640u   // 40 ms @ 16 kHz

typedef enum {
    MN_SOUND_BEEP = 0,   // 电子音：纯正弦
    MN_SOUND_WOOD,       // 木块：非谐和分音 + 快速衰减 + 噪声起音
    MN_SOUND_COWBELL,    // 牛铃：两个主分音（808 风格）+ 较长衰减
    MN_SOUND_COUNT,
} mn_sound_t;

// 各力度的目标峰值（int16）。重音 > 普通拍 > 细分音。
#define MN_PEAK_ACCENT 22000
#define MN_PEAK_BEAT 16500
#define MN_PEAK_SUB 9000
#define MN_PEAK_COUNT 12000   // 开始倒数提示音：所有音色共用，比节拍轻、音高更高

// 把音色 sound、力度 kind（mn_tick_kind_t）的 click 写入 out[0..MN_CLICK_LEN)。
// kind 为 MN_TICK_COUNT 时输出与音色无关的倒数提示音（C7 + 八度泛音的柔和"嘀"）。
// sound / kind 越界时按 0 处理。纯函数，可在任意任务调用；内部使用浮点，只应在初始化时调用。
void mn_sound_render(uint8_t sound, uint8_t kind, int16_t out[MN_CLICK_LEN]);
