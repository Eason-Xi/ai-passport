// main/yz_save.h —— 设置与进度的二进制格式（纯 C，主机可测）。NVS 读写见 yz_store.c。
//
// 两个 blob 都以 2 字节魔数 + 1 字节版本开头、CRC-32 结尾；字数变化、越界取值、
// 校验失败一律视为无效，调用方回退到默认值，绝不使用半解析的数据。
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
bool yz_prog_unpack(yz_progress_t *prog, const uint8_t *buf, size_t len);
