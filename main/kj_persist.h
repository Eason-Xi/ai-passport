// main/kj_persist.h —— 赌局状态的紧凑序列化（庄家掉电 / 重启后恢复）。纯 C。
//
// 只保存可恢复的长期状态：阶段、编号、每位选手的 MAC、手牌、星星、战绩。进行中的挑战 / 对决
// 不保存（恢复后双方回到空闲，暗牌退回手中），在线状态重新由心跳判定。
#pragma once

#include "kj_rules.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KJ_PERSIST_MAGIC   0x314A4B47u   // "GKJ1"
#define KJ_PERSIST_VERSION 1
#define KJ_PERSIST_HEADER  14
#define KJ_PERSIST_RECORD  20
#define KJ_PERSIST_MAX     (KJ_PERSIST_HEADER + KJ_PERSIST_RECORD * KJ_MAX_PLAYERS + 4)
// 恢复时视图版本整体前移，避免与选手手里的旧版本号偶然相同而漏推。
#define KJ_PERSIST_VER_BUMP 500

size_t kj_persist_save(const kj_game_t *g, uint8_t *buf, size_t cap);
// 校验失败返回 false，且不修改 g。
bool kj_persist_load(kj_game_t *g, const uint8_t *buf, size_t len, uint32_t now_ms);
