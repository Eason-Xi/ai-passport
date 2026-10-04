// main/mn_store.h —— 设置持久化：NVS 命名空间 "metronome"，键 "cfg"（带版本与 CRC 的 blob）。
//
// 只由应用任务调用。写入时机受 mn_store_tick 的 quiet 参数控制：节拍器出声期间绝不写 Flash
// （写 Flash 会暂停 Cache，I2S 中断被拖延会断流爆音），运行中的改动在停止后落盘。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mn_cfg.h"

// 初始化 NVS 并读取设置；失败或无存档时 cfg 为默认值。不会擦除 NVS 分区。
void mn_store_init(mn_cfg_t *cfg);
// 标记设置已改变（记录时间，稍后保存）。
void mn_store_mark(uint32_t now_ms);
// 周期调用：已标记、距最后一次修改 ≥ 1.5 s、且 quiet（音频静止）时写入。
void mn_store_tick(const mn_cfg_t *cfg, uint32_t now_ms, bool quiet);
