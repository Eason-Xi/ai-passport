// main/as_theme.h —— "夜空"配色与版式常量。
//
// 深靛蓝到暗紫的渐变背景，夜里看不刺眼；暖月光色只用于焦点与强调；
// 每种声音一个主题色，聆听页的光环、调音台的色点与音量条都用它。
#pragma once

#include <stdint.h>

#include "as_sounds.h"

#define AS_SCREEN_W 240
#define AS_SCREEN_H 320

#define AS_C_BG_TOP 0x0A0F24
#define AS_C_BG_BOTTOM 0x1C1740
#define AS_C_PANEL 0x1A1C3A
#define AS_C_PANEL_HI 0x2B2C5C
#define AS_C_TEXT 0xEEEAF7
#define AS_C_TEXT_DIM 0x9C98BC
#define AS_C_TEXT_FAINT 0x625F86
#define AS_C_MOON 0xF5D79A       // 焦点 / 强调（暖月光）
#define AS_C_MOON_DEEP 0x2A2240  // 月光底色上的文字
#define AS_C_BREATH 0x7CCFC4     // 呼吸引导
#define AS_C_DANGER 0xFF7A6E
#define AS_C_OK 0x7FD69A
#define AS_C_TRACK 0x34355E      // 音量条底槽

static const uint32_t AS_SOUND_COLOR[AS_SOUND_COUNT] = {
    0x6FA8FF,   // 细雨
    0x3FC1C9,   // 海浪
    0xFF8A4C,   // 篝火
    0x5FD3A3,   // 溪流
    0xB7C4D9,   // 山风
    0xA6D96A,   // 虫鸣
    0xE8C268,   // 颂钵
    0xE6E6F0,   // 白噪音
    0xF2A7C3,   // 粉噪音
    0xC0916E,   // 棕噪音
};
#define AS_C_SOUND_NONE 0x4A4870

static inline uint32_t as_sound_color(uint8_t sound) {
    return sound < AS_SOUND_COUNT ? AS_SOUND_COLOR[sound] : AS_C_SOUND_NONE;
}
