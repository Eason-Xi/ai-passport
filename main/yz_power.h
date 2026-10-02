// main/yz_power.h —— 空闲调暗 / 熄屏（纯 C，主机可测）。
//
// 计时临写期间视为忙碌，不调暗；调暗或熄屏时第一下按键只用来唤醒屏幕，不执行操作。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yz_book.h"

#define YZ_DIM_AFTER_MS (90u * 1000u)
#define YZ_OFF_AFTER_MS (300u * 1000u)
#define YZ_DIM_PERCENT 8

typedef enum { YZ_POWER_ON = 0, YZ_POWER_DIM, YZ_POWER_OFF } yz_power_level_t;

typedef struct {
    uint32_t last_ms;
    uint8_t level;   // yz_power_level_t
} yz_power_t;

void yz_power_init(yz_power_t *p, uint32_t now_ms);
// 返回 true 表示这次按键只用于唤醒（调用方应丢弃该按键并恢复亮度）。
bool yz_power_key(yz_power_t *p, uint32_t now_ms);
// 返回 true 表示亮度档位发生变化。
bool yz_power_tick(yz_power_t *p, uint32_t now_ms, bool busy);
uint8_t yz_power_backlight(const yz_power_t *p, const yz_cfg_t *cfg);
