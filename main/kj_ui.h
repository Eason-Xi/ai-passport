// main/kj_ui.h —— 限定猜拳的 LVGL 界面（240×320 竖屏，自绘，不使用 baseline demo 外壳）。
//
// 平台层每轮把状态整理成 kj_ui_model_t 交给 kj_ui_render()；界面层按"签名"判断是否需要重建
// 当前页面，倒计时 / 电量 / 计时等只做局部刷新。本模块只依赖 LVGL 与纯逻辑头文件，
// 因此可以在电脑上用 tools/render_kj_preview.py 渲染出与板上一致的截图。
#pragma once

#include "kj_client.h"
#include "kj_flow.h"
#include "kj_rules.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_UI_OPP_MAX 32
#define KJ_UI_W 240
#define KJ_UI_H 320

typedef struct {
    uint8_t page;                 // kj_page_t
    int8_t battery;               // 0..100；-1 = 读不到
    uint32_t now_ms;
    uint8_t toast;                // kj_toast_t
    bool disconnected;            // 已入座但听不到庄家
    // 首页
    uint8_t title_sel;
    // 找赌局
    kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];
    uint8_t room_count;
    uint8_t room_sel;
    bool joining;
    // 选手
    uint16_t room;
    kj_view_t view;
    uint8_t seated;
    kj_opponent_t opps[KJ_UI_OPP_MAX];
    uint8_t opp_count;
    uint8_t opp_sel;
    uint8_t card_sel;
    uint8_t deadline_s;           // 挑战倒计时（实时）
    // 庄家
    uint16_t host_room;
    uint8_t host_phase;
    uint32_t host_phase_s;        // 当前阶段已持续的秒数
    kj_summary_t host_sum;
    uint8_t host_sel;
    int8_t host_confirm;
    uint8_t host_enabled;         // bit i = kj_host_item_t i 可用
    bool usb;
} kj_ui_model_t;

// 在 LVGL 初始化之后、持有 LVGL 锁时调用。
void kj_ui_init(void);
// 渲染模型（持有 LVGL 锁时调用）。调用方须先 memset 模型再填写，签名按字节计算。
void kj_ui_render(const kj_ui_model_t *m);
// 信号强度分档（0..4），列表与签名共用。
uint8_t kj_ui_signal_level(int8_t rssi);
// toast 文案
const char *kj_ui_toast_text(uint8_t toast);
