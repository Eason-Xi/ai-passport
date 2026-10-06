// main/as_sounds.h —— 10 种实时合成声景（纯 C 定点，主机可测）。
//
// 每种声景都是无限长、不重复的随机过程：没有采样文件，也就没有循环接缝。
// 输出统一归一到 RMS 约 −18 dBFS、峰值留有余量，由混音器再按层音量叠加。
// 单个声部的状态约 200 字节；渲染只用整数运算，16 kHz 下每样本几十到一两百个周期。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "as_dsp.h"

typedef enum {
    AS_SOUND_RAIN = 0,    // 细雨：沙沙雨幕 + 随机雨滴
    AS_SOUND_WAVES,       // 海浪：一浪一浪涌来又退去
    AS_SOUND_FIRE,        // 篝火：低沉火声 + 噼啪爆裂
    AS_SOUND_STREAM,      // 溪流：水泡咕嘟声 + 流水
    AS_SOUND_WIND,        // 山风：音高漂移的呼啸与阵风
    AS_SOUND_CRICKETS,    // 虫鸣：夏夜蟋蟀的唧唧声
    AS_SOUND_BOWL,        // 颂钵：间隔敲击、长余韵
    AS_SOUND_WHITE,       // 白噪音
    AS_SOUND_PINK,        // 粉噪音
    AS_SOUND_BROWN,       // 棕噪音
    AS_SOUND_COUNT,
} as_sound_id_t;

#define AS_SOUND_NONE 0xFF   // 空轨道

#define AS_RAIN_DROPS 8
#define AS_FIRE_CRACKS 6
#define AS_STREAM_BUBBLES 10
#define AS_CRICKETS 3
#define AS_BOWL_PARTIALS 6

typedef struct {
    int32_t env;        // Q30 包络
    int32_t k;          // Q30 衰减因子
    int32_t amp;        // Q15 幅度
    uint32_t phase;
    uint32_t inc;       // 每样本相位增量
    int32_t inc_slope;  // 每样本相位增量的变化（水泡上滑音）
} as_partial_t;

typedef struct {
    uint32_t phase, inc;
    uint32_t next;         // 下一次鸣叫的起始样本
    uint8_t pulses;        // 本次鸣叫剩余脉冲数
    uint32_t pulse_pos;    // 当前脉冲内的位置（样本），脉冲之后为间隔
    int32_t amp;           // Q15
} as_bug_t;

typedef struct {
    uint8_t id;                 // as_sound_id_t
    as_rng_t rng;
    uint32_t t;                 // 已渲染样本数
    union {
        struct {
            as_pink_t pink;
            as_lp1_t hp, lp, drop_lp;
            as_drift_t intensity;
            as_partial_t drops[AS_RAIN_DROPS];
        } rain;
        struct {
            as_lp1_t rumble_a, rumble_b;
            as_svf_t body;
            as_lp1_t hiss_hp;
            uint32_t len, pos, peak;   // 当前这一浪的总长、进度、浪峰位置（样本）
            int32_t height;            // Q15 本浪高度
            int32_t env;               // Q15 当前包络
        } waves;
        struct {
            as_lp1_t rumble_a, rumble_b, hiss_hp, pop_lp;
            as_drift_t flicker;
            as_partial_t cracks[AS_FIRE_CRACKS];
            uint32_t burst;            // 噼啪簇剩余样本：期间爆裂概率升高
        } fire;
        struct {
            as_svf_t flow;
            as_lp1_t flow_lp;
            as_drift_t swirl;
            as_partial_t bubbles[AS_STREAM_BUBBLES];
        } stream;
        struct {
            as_svf_t howl;
            as_lp1_t rumble;
            as_drift_t pitch, gust;
        } wind;
        struct {
            as_pink_t pink;
            as_lp1_t night_lp;
            as_bug_t bugs[AS_CRICKETS];
        } crickets;
        struct {
            as_partial_t partials[AS_BOWL_PARTIALS];
            uint32_t next;             // 下一次敲击的样本时刻
            uint8_t note;              // 上一次的音高序号，避免连续重复
            uint16_t attack;           // 起音剩余样本数
            as_pink_t room;
            as_lp1_t room_lp;
        } bowl;
        struct {
            as_pink_t pink;
            as_lp1_t lp, dc;
        } noise;
    } u;
} as_voice_t;

// 名称只用于日志与测试；界面文字在 as_strings.h。
const char *as_sound_debug_name(uint8_t id);

// 初始化声部。seed 决定随机序列（同 seed 输出逐样本可复现）。
void as_voice_init(as_voice_t *v, uint8_t id, uint32_t seed);

// 渲染 n 个样本（Q15，已限制在 int16 范围）。
void as_voice_render(as_voice_t *v, int16_t *out, size_t n);
