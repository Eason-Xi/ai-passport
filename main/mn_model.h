// main/mn_model.h —— 节拍器应用状态机（纯逻辑，无 ESP-IDF / LVGL 依赖）。
//
// 只有应用任务调用这里的函数（单线程）。每个入口返回一组 effect 标志，由应用任务负责
// 执行副作用：通知音频任务、标记保存、刷新界面。这样按键交互可以完整地在主机上测试。
//
// 页面与按键（UP / DOWN / OK 三键，事件含义见 bsp_button.h）：
//   主界面   UP/DOWN 按下 ±1 BPM，长按加速连调、松手即停；OK 单击 开始/停止
//            （开启"开始倒数"时先倒数 3 秒，倒数中再按 OK 即取消）；
//            OK 双击 敲击测速；OK 长按 打开设置。
//   设置     浏览：UP/DOWN 移动选中行；OK 单击 进入编辑（"敲击测速"行则直接进入测速）；
//            编辑：UP/DOWN 改值（拍号、音量可长按连调）；OK 单击 确认；任意时刻 OK 长按 返回主界面。
//            节拍器在设置中继续运行，改动立即可听。
//   敲击测速 任意键按下 = 敲击；最后一次敲击 2.5 s 后（至少 3 下）自动应用并提示"已设定"，
//            0.9 s 后返回；6 s 无有效敲击则不改动直接返回；OK 长按 取消。
//            进入时暂停播放，返回时恢复原播放状态。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mn_cfg.h"
#include "mn_tap.h"

// 与 bsp_btn_t / bsp_btn_ev_t 数值一致（mn_app.c 用 _Static_assert 保证）。
typedef enum { MN_BTN_UP = 0, MN_BTN_DOWN, MN_BTN_OK, MN_BTN_COUNT } mn_btn_t;
typedef enum { MN_EV_PRESS = 0, MN_EV_CLICK, MN_EV_DOUBLE, MN_EV_LONG, MN_EV_RELEASE } mn_ev_t;

typedef enum { MN_PAGE_MAIN = 0, MN_PAGE_SETTINGS, MN_PAGE_TAP } mn_page_t;

typedef enum {
    MN_ROW_BEATS = 0,   // 拍号（每小节拍数）
    MN_ROW_ACCENT,      // 首拍重音
    MN_ROW_SUBDIV,      // 细分
    MN_ROW_SOUND,       // 音色
    MN_ROW_VOLUME,      // 音量
    MN_ROW_COUNTIN,     // 开始倒数开关
    MN_ROW_TAP,         // 敲击测速入口
    MN_ROW_COUNT,
} mn_row_t;

typedef uint16_t mn_fx_t;
#define MN_FX_RUN (1u << 0)        // running 改变：通知音频开始 / 停止
#define MN_FX_METER (1u << 1)      // BPM / 拍号 / 细分 / 重音改变：通知音频
#define MN_FX_SOUND (1u << 2)      // 音色改变
#define MN_FX_VOLUME (1u << 3)     // 音量改变
#define MN_FX_SAVE (1u << 4)       // 设置改变，需要（择机）保存
#define MN_FX_SCREEN (1u << 5)     // 页面切换：重建页面
#define MN_FX_REFRESH (1u << 6)    // 页面内容刷新
#define MN_FX_TAP_FLASH (1u << 7)  // 敲击反馈动画

#define MN_TAP_APPLY_IDLE_MS 2500u   // 最后一次敲击后多久自动应用
#define MN_TAP_MIN_TAPS 3u           // 自动应用所需的最少敲击次数
#define MN_TAP_GIVEUP_MS 6000u       // 无有效结果时多久放弃返回
#define MN_TAP_DONE_SHOW_MS 900u     // "已设定"提示停留时长
#define MN_REPEAT_GUARD_MS 15000u    // 长按连调的保护上限（万一丢失 RELEASE 事件）

typedef struct {
    mn_cfg_t cfg;
    mn_page_t page;
    bool running;
    bool countin_on_start;  // 本次开始播放是否先倒数（只有 OK 单击开始且设置开启时为真）

    // 设置页
    uint8_t row;          // mn_row_t
    bool editing;

    // 敲击测速
    mn_tap_t tap;
    mn_page_t tap_return; // 结束后返回的页面
    bool tap_resume;      // 结束后是否恢复播放
    bool tap_done;        // 已应用，正在显示"已设定"
    uint16_t tap_result;
    uint32_t tap_enter_ms;
    uint32_t tap_last_ms;
    uint32_t tap_done_ms;

    // 长按连调
    bool rep_active;
    int8_t rep_dir;       // +1 / -1
    uint8_t rep_btn;      // 触发连调的按键，等它的 RELEASE
    uint32_t rep_n;       // 下一次自动步进的序号
    uint32_t rep_due_ms;  // 下一次自动步进的时间
    uint32_t rep_start_ms;
} mn_model_t;

void mn_model_init(mn_model_t *m, const mn_cfg_t *cfg);

// 处理一个按键事件。now_ms 为单调毫秒时间；t_us 为按键回调里记录的 µs 时间戳（测速用）。
mn_fx_t mn_model_key(mn_model_t *m, mn_btn_t btn, mn_ev_t ev, uint32_t now_ms, int64_t t_us);

// 周期调用（长按连调、测速超时）。
mn_fx_t mn_model_tick(mn_model_t *m, uint32_t now_ms);

// 距下一次需要调用 mn_model_tick 的毫秒数（不超过 max_ms），用作应用任务的队列等待超时。
uint32_t mn_model_wait_ms(const mn_model_t *m, uint32_t now_ms, uint32_t max_ms);

// ---------------------------------------------------------------------------
// 空闲省电（只在停止状态下计时，播放中不计）：
//   60 s 无按键 → 背光降到 10%；2 min → 熄屏（背光关闭，状态保留）；
//   10 min → 请求自动关机（应用执行深度睡眠，任意键唤醒后重新开机）。
// 调暗或熄屏时任意键先唤醒屏幕，唤醒用的这次按键（含随后的 RELEASE 和延迟
// 到达的 CLICK / DOUBLE）被吞掉，不触发功能。
// ---------------------------------------------------------------------------

#define MN_DIM_AFTER_MS 60000u
#define MN_SCREEN_OFF_AFTER_MS 120000u
#define MN_POWER_OFF_AFTER_MS 600000u
#define MN_BACKLIGHT_ON 100u
#define MN_BACKLIGHT_DIM 10u
#define MN_BACKLIGHT_OFF 0u
#define MN_WAKE_SWALLOW_MS 400u

typedef enum {
    MN_POWER_ON = 0,       // 正常亮度
    MN_POWER_DIM,          // 调暗
    MN_POWER_SCREEN_OFF,   // 熄屏
    MN_POWER_SHUTDOWN,     // 请求自动关机
} mn_power_level_t;

typedef struct {
    uint32_t last_ms;      // 最近一次按键（或播放中）的时间
    uint8_t level;         // mn_power_level_t
    int8_t wake_btn;       // 正在被吞掉的唤醒按键，-1 表示无
    bool wake_released;
    uint32_t release_ms;
} mn_power_t;

void mn_power_init(mn_power_t *p, uint32_t now_ms);
// 返回 true 表示这个事件被唤醒逻辑吞掉，不应再交给 mn_model_key。
bool mn_power_key(mn_power_t *p, mn_btn_t btn, mn_ev_t ev, uint32_t now_ms);
// busy（正在播放）时不降级并重新计时。返回等级是否改变（背光需更新，或进入 SHUTDOWN）。
// 等级只会随空闲时间升高；回到 MN_POWER_ON 只能通过按键或 mn_power_wake。
bool mn_power_tick(mn_power_t *p, uint32_t now_ms, bool busy);
// 关机被取消（例如入睡时有键按住）等情况下回到正常亮度并重新计时。
void mn_power_wake(mn_power_t *p, uint32_t now_ms);
uint8_t mn_power_backlight(const mn_power_t *p);

// ---------------------------------------------------------------------------
// 低电量自动关机：电量计读数 ≤ 3% 连续 3 次（应用每 10 s 读一次）才判定，
// 避免单次读数抖动误关机。以下情况视为外部供电、不判定：
//   * 连接着电脑 USB（usb_host 为真）；
//   * 低电量期间电池电压比第一次低电量读数上升超过 30 mV（插着充电头在充电）。
// 读数不可用（-1）时清零计数，永不触发。
// ---------------------------------------------------------------------------

#define MN_LOWBATT_SOC 3
#define MN_LOWBATT_SAMPLES 3u
#define MN_LOWBATT_RISE_MV 30
#define MN_LOWBATT_NOTICE_MS 3000u   // 关机前提示停留时长

typedef struct {
    uint8_t count;     // 连续低电量读数次数
    int first_mv;      // 本轮第一次低电量时的电池电压（-1 未知）
} mn_lowbatt_t;

void mn_lowbatt_init(mn_lowbatt_t *b);
// 喂入一次读数；返回 true 表示应当提示并关机。
bool mn_lowbatt_feed(mn_lowbatt_t *b, int soc, int mv, bool usb_host);
