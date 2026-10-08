// main/as_cfg.h —— 应用设置：三层混音、主音量、睡眠定时、呼吸节奏（纯 C，主机可测）。
//
// 存档格式（NVS blob，固定 16 字节，小端）：
//   [0..1] 'A' 'S'   [2] 版本   [3..5] 三层声音   [6..8] 三层音量
//   [9] 主音量   [10] 定时档位   [11] 呼吸节奏   [12..15] 前 12 字节的 CRC-32
// 任何字段越界、魔数 / 版本 / CRC 不符都整体回退默认值，不会读出半套设置。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "as_mixer.h"

#define AS_CFG_VERSION 1
#define AS_CFG_BLOB_SIZE 16
#define AS_VOLUME_MIN 1
#define AS_VOLUME_MAX 10

// 睡眠定时档位。
typedef enum {
    AS_TIMER_OFF = 0,
    AS_TIMER_15,
    AS_TIMER_30,
    AS_TIMER_45,
    AS_TIMER_60,
    AS_TIMER_90,
    AS_TIMER_COUNT,
} as_timer_opt_t;

// 呼吸节奏。
typedef enum {
    AS_BREATH_478 = 0,   // 吸 4 · 屏 7 · 呼 8
    AS_BREATH_EVEN,      // 吸 5 · 呼 5
    AS_BREATH_BOX,       // 吸 4 · 屏 4 · 呼 4 · 屏 4
    AS_BREATH_COUNT,
} as_breath_pattern_t;

typedef struct {
    uint8_t sound[AS_LAYERS];   // as_sound_id_t 或 AS_SOUND_NONE
    uint8_t level[AS_LAYERS];   // 0..AS_LEVEL_MAX
    uint8_t volume;             // AS_VOLUME_MIN..AS_VOLUME_MAX
    uint8_t timer;              // as_timer_opt_t
    uint8_t breath;             // as_breath_pattern_t
} as_cfg_t;

void as_cfg_default(as_cfg_t *cfg);
// 逐项检查范围；越界项改为默认值并返回 false。
bool as_cfg_sanitize(as_cfg_t *cfg);
size_t as_cfg_pack(const as_cfg_t *cfg, uint8_t *buf, size_t cap);
// 解码成功返回 true；失败时 cfg 为默认值。
bool as_cfg_unpack(as_cfg_t *cfg, const uint8_t *buf, size_t len);

// 主音量档位 → codec 音量百分比。codec 默认曲线是 0..100% 线性对应 −50..0 dB，
// 这里 1 档 ≈ −29 dB（夜里很轻），之后每档约 +3.2 dB，10 档 = 100%。
uint8_t as_cfg_volume_percent(uint8_t volume);
// 定时档位 → 分钟（关闭为 0）。
uint16_t as_timer_minutes(uint8_t opt);
// 至少有一层"有声音且音量 > 0"。
bool as_cfg_audible(const as_cfg_t *cfg);
