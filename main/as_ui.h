// main/as_ui.h —— ASMR 声景播放器界面（LVGL）。
//
// 线程约定：所有函数都会访问 LVGL 对象，必须在 LVGL 任务内（lv_timer 回调）或持有
// bsp_lvgl_lock() 时调用。本模块不依赖 ESP-IDF，主机预览程序直接链接它。
//
// 只有一个 screen：页面切换（as_ui_show）清空后按 m->page 重建；菜单是叠在聆听页上的
// 底部抽屉。"晚安"画面与低电量提示是覆盖层。动画由 as_ui_animate 每帧驱动，
// 它只读取 show / update 时拷贝下来的界面快照，不访问应用任务的状态机。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "as_model.h"

// 创建 screen 并加载。audio_ok 为假时聆听页显示"音频不可用"。
void as_ui_init(bool audio_ok);
// 页面切换（AS_FX_SCREEN）。
void as_ui_show(const as_model_t *m, uint32_t now_ms);
// 同页内容刷新（AS_FX_REFRESH）。
void as_ui_update(const as_model_t *m, uint32_t now_ms);
// 电量百分比；-1 表示不可用（隐藏）。
void as_ui_set_battery(int soc);
// 低电量关机提示（覆盖在所有页面之上）。
void as_ui_lowbatt(bool show);
// 每帧调用：meters = 三层电平 + 混音输出电平（0..255）。screen_on 为假时跳过动画（熄屏省电）。
void as_ui_animate(uint32_t now_ms, const uint8_t meters[4], bool screen_on);
