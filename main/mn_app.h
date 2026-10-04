// main/mn_app.h —— 节拍器应用入口：读取设置、启动音频任务与应用任务、建立界面、接入按键。
#pragma once

#include <stdbool.h>

// 在 BSP 显示与 LVGL 初始化成功之后由 app_main 调用一次。
// audio_ok / battery_ok 为对应外设的初始化结果，失败时应用降级运行（无声 / 隐藏电量）。
void mn_app_start(bool audio_ok, bool battery_ok);
