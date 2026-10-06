// main/as_app.h —— ASMR 声景播放器应用入口。
#pragma once

#include <stdbool.h>

// 显示与 LVGL 已初始化后调用。audio_ok / battery_ok 为对应外设初始化结果（失败时降级运行）。
void as_app_start(bool audio_ok, bool battery_ok);
