// main/mn_beat.h —— 音频任务交给界面的节拍事件（纯数据，无依赖）。
#pragma once

#include <stdint.h>

typedef struct {
    int64_t play_us;    // 预计从扬声器发声的时刻（esp_timer 时基，µs）
    uint32_t beat_no;   // 自开始以来的拍计数
    uint16_t bpm;
    uint8_t beat;       // 小节内拍序号 0..beats-1
    uint8_t sub;        // 拍内细分序号 0..subdiv-1
    uint8_t subdiv;
    uint8_t beats;
    uint8_t kind;       // mn_tick_kind_t
} mn_beat_t;
