// main/yz_save.c —— 设置与进度的打包 / 解包，格式见 yz_save.h。
#include "yz_save.h"

#include <string.h>

#define CFG_MAGIC0 'Y'
#define CFG_MAGIC1 'C'
#define PROG_MAGIC0 'Y'
#define PROG_MAGIC1 'P'
#define VERSION 1           // 设置 blob 版本
#define VERSION_PROG 3      // 进度 blob 版本（见 yz_save.h）

uint32_t yz_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint16_t get_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool crc_ok(const uint8_t *buf, size_t len) {
    return get_u32(buf + len - 4) == yz_crc32(buf, len - 4);
}

size_t yz_cfg_pack(const yz_cfg_t *cfg, uint8_t *buf, size_t cap) {
    if (cap < YZ_CFG_BLOB_SIZE) return 0;
    uint8_t *p = buf;
    *p++ = CFG_MAGIC0;
    *p++ = CFG_MAGIC1;
    *p++ = VERSION;
    *p++ = cfg->grid;
    *p++ = cfg->ink;
    *p++ = cfg->timer_idx;
    *p++ = cfg->auto_next ? 1 : 0;
    *p++ = cfg->sound ? 1 : 0;
    *p++ = cfg->bright_idx;
    put_u32(p, yz_crc32(buf, (size_t)(p - buf)));
    return YZ_CFG_BLOB_SIZE;
}

bool yz_cfg_unpack(yz_cfg_t *cfg, const uint8_t *buf, size_t len) {
    if (len != YZ_CFG_BLOB_SIZE || buf[0] != CFG_MAGIC0 || buf[1] != CFG_MAGIC1 || buf[2] != VERSION) {
        return false;
    }
    if (!crc_ok(buf, len)) return false;
    const yz_cfg_t c = {
        .grid = buf[3], .ink = buf[4], .timer_idx = buf[5],
        .auto_next = buf[6], .sound = buf[7], .bright_idx = buf[8],
    };
    if (c.grid >= YZ_GRID_COUNT || c.ink >= YZ_INK_COUNT || c.timer_idx >= YZ_TIMER_OPTIONS ||
        c.auto_next > 1 || c.sound > 1 || c.bright_idx >= YZ_BRIGHT_OPTIONS) {
        return false;
    }
    *cfg = c;
    return true;
}

size_t yz_prog_pack(const yz_progress_t *prog, uint8_t *buf, size_t cap) {
    if (cap < YZ_PROG_BLOB_SIZE) return 0;
    uint8_t *p = buf;
    *p++ = PROG_MAGIC0;
    *p++ = PROG_MAGIC1;
    *p++ = VERSION_PROG;
    put_u16(p, YZ_ENTRY_COUNT);
    p += 2;
    put_u16(p, prog->current);
    p += 2;
    memcpy(p, prog->count, YZ_ENTRY_COUNT);
    p += YZ_ENTRY_COUNT;
    memcpy(p, prog->fav, YZ_FAV_BYTES);
    p += YZ_FAV_BYTES;
    put_u32(p, prog->sessions);
    p += 4;
    put_u32(p, prog->seconds);
    p += 4;
    put_u32(p, yz_crc32(buf, (size_t)(p - buf)));
    return YZ_PROG_BLOB_SIZE;
}

bool yz_prog_unpack(yz_progress_t *prog, const uint8_t *buf, size_t len) {
    if (len != YZ_PROG_BLOB_SIZE || buf[0] != PROG_MAGIC0 || buf[1] != PROG_MAGIC1 || buf[2] != VERSION_PROG) {
        return false;
    }
    if (!crc_ok(buf, len) || get_u16(buf + 3) != YZ_ENTRY_COUNT) return false;
    const uint16_t current = get_u16(buf + 5);
    if (current >= YZ_ENTRY_COUNT) return false;
    const uint8_t *fav = buf + 7 + YZ_ENTRY_COUNT;
    // 位图里下标 >= YZ_ENTRY_COUNT 的位必须为 0。
    for (int i = YZ_ENTRY_COUNT; i < YZ_FAV_BYTES * 8; i++) {
        if ((fav[i / 8] >> (i % 8)) & 1u) return false;
    }
    prog->current = current;
    memcpy(prog->count, buf + 7, YZ_ENTRY_COUNT);
    memcpy(prog->fav, fav, YZ_FAV_BYTES);
    prog->sessions = get_u32(fav + YZ_FAV_BYTES);
    prog->seconds = get_u32(fav + YZ_FAV_BYTES + 4);
    return true;
}
