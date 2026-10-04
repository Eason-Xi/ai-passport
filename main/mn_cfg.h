// main/mn_cfg.h —— 节拍器设置：默认值、合法范围、带版本与 CRC 的存档编解码（纯逻辑）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mn_sched.h"

#define MN_VOLUME_MAX 10u
#define MN_CFG_VERSION 1u
#define MN_CFG_BLOB_SIZE 15u   // 'M' 'N' ver bpm(2) beats accent subdiv sound volume pad crc(4)

typedef struct {
    uint16_t bpm;     // 30–250，默认 100
    uint8_t beats;    // 每小节拍数 1–12，默认 4
    uint8_t accent;   // 首拍重音 0/1，默认 1
    uint8_t subdiv;   // 细分 1/2/3/4，默认 1
    uint8_t sound;    // 音色 mn_sound_t，默认木块
    uint8_t volume;   // 音量 0–10（0 = 静音），默认 7
} mn_cfg_t;

void mn_cfg_default(mn_cfg_t *cfg);
// 越界字段逐项回落为默认值；返回是否所有字段原本都合法。
bool mn_cfg_sanitize(mn_cfg_t *cfg);
mn_meter_t mn_cfg_meter(const mn_cfg_t *cfg);

// 编码到 buf（至少 MN_CFG_BLOB_SIZE 字节），返回写入字节数；cap 不足时返回 0。
size_t mn_cfg_pack(const mn_cfg_t *cfg, uint8_t *buf, size_t cap);
// 解码；长度、魔数、版本、CRC 或字段范围任一不对都返回 false，此时 cfg 保持默认值。
bool mn_cfg_unpack(mn_cfg_t *cfg, const uint8_t *buf, size_t len);

// 音量档位 → codec 音量百分比（bsp_audio_set_volume 的参数）。0 档为 0（同时静音输出）。
uint8_t mn_cfg_volume_percent(uint8_t level);
