// main/yz_store.h —— NVS 读写：命名空间 "yzcopy"，键 "cfg"、"prog" 各一个带 CRC 的 blob。
//
// 改动先标记为“脏”，静置 SAVE_DELAY 后在应用任务里合并写入；播放提示音时推迟，
// 避免 Flash 写入暂停 Cache 造成 I2S 断流。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "yz_book.h"

// 读取存档；无存档或损坏时填默认值。NVS 不可用时本次运行只在内存中保存。
void yz_store_init(yz_cfg_t *cfg, yz_progress_t *prog);
void yz_store_mark(bool cfg_dirty, bool prog_dirty, uint32_t now_ms);
void yz_store_tick(const yz_cfg_t *cfg, const yz_progress_t *prog, uint32_t now_ms, bool busy);
