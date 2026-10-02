// main/yz_app.h —— 字帖应用：读取存档、建立界面、启动应用任务并接入按键。
#pragma once

#include <stdbool.h>

// 显示与 LVGL 必须已初始化。audio_ok / battery_ok 为对应 BSP 初始化结果。
void yz_app_start(bool audio_ok, bool battery_ok);
