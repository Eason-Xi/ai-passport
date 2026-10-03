// main/yz_save.h —— 设置与进度的二进制格式（纯 C，主机可测）。NVS 读写见 yz_store.c。
//
// 两个 blob 都以 2 字节魔数 + 1 字节版本开头、CRC-32 结尾；越界取值、校验失败一律视为
// 无效，调用方回退到默认值，绝不使用半解析的数据。
//
// 进度 v3（《多宝塔碑》全碑）：
//   'Y' 'P' 3 | u16 字数 | u16 当前字 | u8×字数 临写遍数 | 收藏位图 | u32 遍数 | u32 秒数 | u32 CRC
// 字数必须等于 YZ_ENTRY_COUNT。v1 / v2 是三本字帖时期的旧固件写的，字下标对应旧字目，
// 不再读取（视为无效，进度从头开始；设置 blob 格式未变，照常保留）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yz_book.h"

#define YZ_CFG_BLOB_SIZE (3 + 6 + 4)
#define YZ_PROG_BLOB_SIZE (3 + 2 + 2 + YZ_ENTRY_COUNT + YZ_FAV_BYTES + 4 + 4 + 4)

uint32_t yz_crc32(const uint8_t *data, size_t len);

size_t yz_cfg_pack(const yz_cfg_t *cfg, uint8_t *buf, size_t cap);
bool yz_cfg_unpack(yz_cfg_t *cfg, const uint8_t *buf, size_t len);

size_t yz_prog_pack(const yz_progress_t *prog, uint8_t *buf, size_t cap);
// 只接受字数相符的 v3；失败时不改动 prog。
bool yz_prog_unpack(yz_progress_t *prog, const uint8_t *buf, size_t len);
