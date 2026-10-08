// main/as_model.h —— 应用状态机：页面、按键、播放、睡眠定时（纯 C，主机可测）。
//
// 只由应用任务调用。按键与周期 tick 输入，返回副作用位（as_fx_t），由 as_app 执行：
// 下发音频请求、写存档、刷新界面、控制屏幕。状态机本身不碰硬件、不读时钟。
//
// 按键（三键：▲ UP、▼ DOWN、● OK）：
//   聆听页   ▲▼ 主音量（按住连调）· OK 播放/暂停 · 双击 OK 呼吸引导 · 长按 OK 菜单
//   菜单     ▲▼ 选择 · OK 进入 · 长按 OK 返回
//   调音台   ▲▼ 选轨道 · OK 换声音 ·（选好声音后）▲▼ 调层音量、OK 确定 · 长按 OK 返回
//   选声音   ▲▼ 选择（播放中实时试听）· OK 确定 · 长按 OK 取消
//   睡眠定时 ▲▼ 选择档位 · OK 确定 · 长按 OK 取消
//   呼吸引导 ▲▼ 切换节奏 · OK / 长按 OK 返回
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "as_breath.h"
#include "as_cfg.h"

typedef enum { AS_BTN_UP = 0, AS_BTN_DOWN, AS_BTN_OK } as_btn_t;
typedef enum { AS_EV_PRESS = 0, AS_EV_CLICK, AS_EV_DOUBLE, AS_EV_LONG, AS_EV_RELEASE } as_ev_t;

typedef enum {
    AS_PAGE_HOME = 0,
    AS_PAGE_MENU,
    AS_PAGE_MIXER,
    AS_PAGE_PICKER,
    AS_PAGE_TIMER,
    AS_PAGE_BREATH,
} as_page_t;

typedef enum {
    AS_MENU_MIXER = 0,
    AS_MENU_TIMER,
    AS_MENU_BREATH,
    AS_MENU_BACK,
    AS_MENU_COUNT,
} as_menu_item_t;

#define AS_MIX_DONE AS_LAYERS                  // 调音台最后一行"完成"
#define AS_PICK_NONE_INDEX AS_SOUND_COUNT      // 选声音列表最后一项"空"
#define AS_PICK_COUNT (AS_SOUND_COUNT + 1)

#define AS_HOLD_REPEAT_MS 180u       // 按住连调的步进间隔
#define AS_VOLUME_OVERLAY_MS 1500u
#define AS_NIGHT_MS 3000u            // "晚安"画面停留时长
#define AS_TICK_SLOW_MS 200u
#define AS_TICK_FAST_MS 40u

typedef enum {
    AS_FX_NONE = 0,
    AS_FX_AUDIO = 1u << 0,     // 混音层 / 播放状态 / 渐弱变化：下发音频请求
    AS_FX_VOLUME = 1u << 1,    // 主音量变化：更新 codec 音量
    AS_FX_SAVE = 1u << 2,      // 设置变化：稍后写存档
    AS_FX_SCREEN = 1u << 3,    // 页面切换：重建界面
    AS_FX_REFRESH = 1u << 4,   // 同页内容变化：更新控件
    AS_FX_NIGHT = 1u << 5,     // 定时结束："晚安"画面开始（应用应点亮屏幕）
    AS_FX_NIGHT_END = 1u << 6, // "晚安"画面结束（应用应熄屏）
} as_fx_bits_t;
typedef uint32_t as_fx_t;

typedef struct {
    as_cfg_t cfg;
    uint8_t page;            // as_page_t
    bool playing;

    uint8_t menu_sel;        // as_menu_item_t
    uint8_t mix_sel;         // 0..AS_LAYERS（AS_MIX_DONE = 完成）
    bool mix_edit;           // 正在调节 mix_sel 这一层的音量
    uint8_t pick_sel;        // 0..AS_PICK_COUNT-1
    uint8_t pick_orig;       // 进入选声音前的声音（取消时恢复）
    uint8_t timer_sel;       // 睡眠定时页当前选中的档位

    // 睡眠定时：播放时倒数，暂停时冻结剩余时间，到点渐弱并停止。
    bool timer_armed;        // 有定时（cfg.timer != OFF）且尚未到点
    uint32_t timer_left_ms;  // 暂停时的剩余时间
    uint32_t timer_end_ms;   // 播放中的到点时刻
    int32_t fade;            // Q15 当前渐弱增益

    uint32_t shown_left_s;   // 界面上显示的剩余秒数（变化时刷新）

    uint32_t breath_start_ms;
    bool volume_shown;       // 音量浮层
    uint32_t volume_until_ms;
    bool night;              // "晚安"画面
    uint32_t night_until_ms;

    int8_t hold_btn;         // 按住连调中的按键，-1 无
    uint32_t hold_next_ms;
} as_model_t;

void as_model_init(as_model_t *m, const as_cfg_t *cfg, uint32_t now_ms);
as_fx_t as_model_key(as_model_t *m, as_btn_t btn, as_ev_t ev, uint32_t now_ms);
as_fx_t as_model_tick(as_model_t *m, uint32_t now_ms);
// 应用任务下一次需要 tick 的最长等待（ms）。
uint32_t as_model_wait_ms(const as_model_t *m);

// 睡眠定时剩余毫秒；未设定时返回 0。
uint32_t as_model_timer_left(const as_model_t *m, uint32_t now_ms);
// 当前是否应保持常亮："晚安"画面期间，以及进入（或切换节奏）呼吸引导后的前 5 分钟。
// 之后即使停在呼吸引导页也按正常规则调暗熄屏——用户可能已经睡着，屏幕不能整夜亮着。
#define AS_BREATH_KEEP_MS (5u * 60u * 1000u)
bool as_model_keep_screen(const as_model_t *m, uint32_t now_ms);
// 选声音列表索引 ↔ 声音编号。
uint8_t as_pick_to_sound(uint8_t index);
uint8_t as_sound_to_pick(uint8_t sound);
