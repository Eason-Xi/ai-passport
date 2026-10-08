// main/as_power.h —— 屏幕省电、自动关机与低电量判定（纯 C，主机可测）。
//
// 两套计时（自最近一次按键起）：
//   正在播放：20 s 调暗 → 45 s 熄屏；不会自动关机（声音继续，由睡眠定时负责停止）。
//   已停止：  60 s 调暗 → 2 min 熄屏 → 10 min 请求关机（深度睡眠，任意键唤醒重新开机）。
//   保持常亮（"晚安"画面、呼吸引导的前 5 分钟，见 as_model_keep_screen）：不计时。
// 调暗或熄屏时任意键先唤醒屏幕；这次按键（含随后的 RELEASE 和延迟到达的
// CLICK / DOUBLE / LONG）被吞掉，不触发功能，避免摸黑误操作。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AS_PLAY_DIM_MS 20000u
#define AS_PLAY_OFF_MS 45000u
#define AS_IDLE_DIM_MS 60000u
#define AS_IDLE_OFF_MS 120000u
#define AS_IDLE_SHUTDOWN_MS 600000u
#define AS_WAKE_SWALLOW_MS 400u

#define AS_BACKLIGHT_ON 100u
#define AS_BACKLIGHT_DIM 8u
#define AS_BACKLIGHT_OFF 0u

typedef enum {
    AS_POWER_ON = 0,
    AS_POWER_DIM,
    AS_POWER_SCREEN_OFF,
    AS_POWER_SHUTDOWN,
} as_power_level_t;

typedef enum {
    AS_ACT_STOPPED = 0,
    AS_ACT_PLAYING,
    AS_ACT_KEEP_ON,
} as_activity_t;

typedef struct {
    uint32_t last_ms;
    uint8_t level;
    int8_t wake_btn;       // 正在被吞掉的唤醒按键，-1 表示无
    bool wake_released;
    uint32_t release_ms;
} as_power_t;

// 按键事件编号与 BSP 一致（见 as_model.h）。
void as_power_init(as_power_t *p, uint32_t now_ms);
// 返回 true 表示事件被唤醒逻辑吞掉，不应交给状态机。
bool as_power_key(as_power_t *p, uint8_t btn, uint8_t ev, uint32_t now_ms);
// 返回等级是否改变（需要更新背光，或已进入 SHUTDOWN）。等级只随空闲时间升高。
bool as_power_tick(as_power_t *p, uint32_t now_ms, as_activity_t act);
// 回到正常亮度并重新计时（关机被取消、"晚安"画面开始等）。
void as_power_wake(as_power_t *p, uint32_t now_ms);
// 直接进入指定等级并从现在重新计时（"晚安"画面结束后立即熄屏）。
void as_power_force(as_power_t *p, uint8_t level, uint32_t now_ms);
uint8_t as_power_backlight(const as_power_t *p);

// ---- 低电量 ----
// 电量 ≤ 3% 连续 3 次（应用每 10 s 读一次）才判定；接电脑 USB，或低电量期间电压
// 比第一次读数上升超过 30 mV（插着充电头）视为外部供电，不判定。读数 −1 时清零。
#define AS_LOWBATT_SOC 3
#define AS_LOWBATT_SAMPLES 3u
#define AS_LOWBATT_RISE_MV 30
#define AS_LOWBATT_NOTICE_MS 3000u

typedef struct {
    uint8_t count;
    int first_mv;
} as_lowbatt_t;

void as_lowbatt_init(as_lowbatt_t *b);
bool as_lowbatt_feed(as_lowbatt_t *b, int soc, int mv, bool usb_host);
