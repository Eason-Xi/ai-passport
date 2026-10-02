// main/yz_save.h —— 设置与进度的二进制格式（纯 C，主机可测）。NVS 读写见 yz_store.c。
//
// 两个 blob 都以 2 字节魔数 + 1 字节版本开头、CRC-32 结尾；越界取值、校验失败一律视为
// 无效，调用方回退到默认值，绝不使用半解析的数据。
//
// 进度 v2（多本字帖）：
//   'Y' 'P' 2 | u8 字帖数 | u16 字数 | u8 当前字帖 | u16×字帖数 各帖当前字 |
//   u8×字数 临写遍数 | 收藏位图 | u32×字帖数 遍数 | u32×字帖数 秒数 | u32 CRC
// 字帖较少的旧版 v2（例如只有多宝塔碑与颜勤礼碑）可直接读取，新增字帖从零开始。
// 进度 v1（只有《多宝塔碑》的旧固件）仍可读取：字数不超过第一本字帖时，全部记到第一本。
// 新字帖只追加在字目末尾，旧存档的字下标因此保持不变。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "yz_book.h"

#define YZ_CFG_BLOB_SIZE (3 + 6 + 4)
#define YZ_PROG_BLOB_SIZE \
    (3 + 1 + 2 + 1 + 2 * YZ_BOOK_COUNT + YZ_ENTRY_COUNT + YZ_FAV_BYTES + 8 * YZ_BOOK_COUNT + 4)
// v1 blob 的最大长度（字数不超过第一本字帖）。
#define YZ_PROG_V1_MAX_SIZE (3 + 2 + 2 + YZ_ENTRY_COUNT + YZ_FAV_BYTES + 4 + 4 + 4)
#define YZ_PROG_READ_MAX \
    (YZ_PROG_BLOB_SIZE > YZ_PROG_V1_MAX_SIZE ? YZ_PROG_BLOB_SIZE : YZ_PROG_V1_MAX_SIZE)

uint32_t yz_crc32(const uint8_t *data, size_t len);

size_t yz_cfg_pack(const yz_cfg_t *cfg, uint8_t *buf, size_t cap);
bool yz_cfg_unpack(yz_cfg_t *cfg, const uint8_t *buf, size_t len);

size_t yz_prog_pack(const yz_progress_t *prog, uint8_t *buf, size_t cap);
// 接受 v2 与可迁移的 v1；失败时不改动 prog。
bool yz_prog_unpack(yz_progress_t *prog, const uint8_t *buf, size_t len);
