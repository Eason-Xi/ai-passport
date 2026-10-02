// main/yz_bell.h —— 计时结束的“磬”声合成（纯 C 定点运算，主机可测）。
//
// 三个非谐泛音叠加，各自指数衰减；起音 8 ms 线性淡入、末尾 60 ms 淡出到 0，
// 避免喇叭“咔嗒”声。按块渲染，调用方把每块直接写给 I2S。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define YZ_BELL_RATE 16000
#define YZ_BELL_MS 1800
#define YZ_BELL_SAMPLES (YZ_BELL_RATE * YZ_BELL_MS / 1000)

typedef struct {
    uint32_t n;              // 已渲染样本数
    uint32_t phase[3];       // Q32 相位
    uint32_t step[3];        // Q32 相位增量
    int32_t env[3];          // Q30 包络
    int32_t decay[3];        // Q30 每样本衰减系数
    int16_t peak;
} yz_bell_t;

void yz_bell_start(yz_bell_t *bell, int16_t peak);
// 渲染最多 cap 个样本，返回实际数量；0 表示结束。
size_t yz_bell_render(yz_bell_t *bell, int16_t *out, size_t cap);
