// main/mn_crc32.h —— CRC-32（IEEE 802.3，反射多项式 0xEDB88320），用于设置存档校验。
#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t mn_crc32(const void *data, size_t len);
