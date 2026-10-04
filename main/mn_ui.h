// main/mn_ui.h —— 节拍器界面（LVGL）。
//
// 线程约定：所有函数都会访问 LVGL 对象，必须在 LVGL 任务内（lv_timer 回调）或持有
// bsp_lvgl_lock() 时调用。本模块不依赖 ESP-IDF，主机预览程序直接链接它。
//
// 界面只有一个 screen：顶栏（拍号摘要 + 电量）与主界面常驻；设置面板从底部滑出覆盖下半屏，
// 敲击测速页全屏覆盖。页面切换只增删覆盖层，不删除主界面。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mn_beat.h"
#include "mn_model.h"

// 创建 screen 与主界面并加载。audio_ok 为假时顶栏显示"音频不可用"。
void mn_ui_init(bool audio_ok);
// 页面切换（MN_FX_SCREEN）：按 m->page 建立 / 移除覆盖层，然后刷新内容。
void mn_ui_show(const mn_model_t *m);
// 内容刷新（MN_FX_REFRESH）。
void mn_ui_update(const mn_model_t *m);
// 电量百分比；-1 表示不可用（隐藏）。
void mn_ui_set_battery(int soc);
// 低电量关机提示：全屏覆盖在所有页面之上；show 为假时移除。
void mn_ui_lowbatt_notice(bool show);
// 敲击测速页的敲击闪光。
void mn_ui_tap_flash(int64_t now_us);

// 一个 tick 到达发声时刻：点亮节拍点、细分点，摆杆闪光。
void mn_ui_beat(const mn_beat_t *b, int64_t now_us);
// 每帧调用（约 20 ms）：更新摆杆位置与闪光衰减。
void mn_ui_animate(int64_t now_us);
