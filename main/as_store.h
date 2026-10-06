// main/as_store.h —— 设置持久化：NVS 命名空间 "asmr"，键 "cfg"（带版本与 CRC 的 blob）。
//
// 只由应用任务调用。写入时机受 as_store_tick 的 quiet 参数控制：出声期间绝不写 Flash
// （写 Flash 会暂停 Cache，I2S 中断被拖延会断流爆音），播放中的改动在暂停 / 定时结束后落盘，
// 自动关机前再 flush 一次。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "as_cfg.h"

// 初始化 NVS 并读取设置；失败或无存档时 cfg 为默认值。不会擦除 NVS 分区。
void as_store_init(as_cfg_t *cfg);
// 标记设置已改变（记录时间，稍后保存）。
void as_store_mark(uint32_t now_ms);
// 周期调用：已标记、距最后一次修改 ≥ 1.5 s、且 quiet（音频静止）时写入。
void as_store_tick(const as_cfg_t *cfg, uint32_t now_ms, bool quiet);
// 关机前调用：有未保存的修改就立即写入。调用方保证音频已静止。
void as_store_flush(const as_cfg_t *cfg);
