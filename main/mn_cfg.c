// main/mn_cfg.c —— 设置默认值与存档编解码，说明见 mn_cfg.h。
#include "mn_cfg.h"

#include "mn_crc32.h"
#include "mn_sound.h"

void mn_cfg_default(mn_cfg_t *cfg) {
    *cfg = (mn_cfg_t){
        .bpm = 100, .beats = 4, .accent = 1, .subdiv = 1, .sound = MN_SOUND_WOOD, .volume = 7,
    };
}

bool mn_cfg_sanitize(mn_cfg_t *cfg) {
    mn_cfg_t def;
    mn_cfg_default(&def);
    bool ok = true;
    if (cfg->bpm < MN_BPM_MIN || cfg->bpm > MN_BPM_MAX) { cfg->bpm = def.bpm; ok = false; }
    if (cfg->beats < MN_BEATS_MIN || cfg->beats > MN_BEATS_MAX) { cfg->beats = def.beats; ok = false; }
    if (cfg->accent > 1) { cfg->accent = def.accent; ok = false; }
    if (cfg->subdiv < 1 || cfg->subdiv > MN_SUBDIV_MAX) { cfg->subdiv = def.subdiv; ok = false; }
    if (cfg->sound >= MN_SOUND_COUNT) { cfg->sound = def.sound; ok = false; }
    if (cfg->volume > MN_VOLUME_MAX) { cfg->volume = def.volume; ok = false; }
    return ok;
}

mn_meter_t mn_cfg_meter(const mn_cfg_t *cfg) {
    return (mn_meter_t){
        .bpm = cfg->bpm, .beats = cfg->beats, .subdiv = cfg->subdiv, .accent = cfg->accent != 0,
    };
}

size_t mn_cfg_pack(const mn_cfg_t *cfg, uint8_t *buf, size_t cap) {
    if (cap < MN_CFG_BLOB_SIZE) return 0;
    buf[0] = 'M';
    buf[1] = 'N';
    buf[2] = MN_CFG_VERSION;
    buf[3] = (uint8_t)(cfg->bpm & 0xFF);
    buf[4] = (uint8_t)(cfg->bpm >> 8);
    buf[5] = cfg->beats;
    buf[6] = cfg->accent;
    buf[7] = cfg->subdiv;
    buf[8] = cfg->sound;
    buf[9] = cfg->volume;
    buf[10] = 0;   // 保留
    const uint32_t crc = mn_crc32(buf, 11);
    for (int i = 0; i < 4; i++) buf[11 + i] = (uint8_t)(crc >> (8 * i));
    return MN_CFG_BLOB_SIZE;
}

bool mn_cfg_unpack(mn_cfg_t *cfg, const uint8_t *buf, size_t len) {
    mn_cfg_default(cfg);
    if (len != MN_CFG_BLOB_SIZE || buf[0] != 'M' || buf[1] != 'N' || buf[2] != MN_CFG_VERSION) return false;
    uint32_t crc = 0;
    for (int i = 0; i < 4; i++) crc |= (uint32_t)buf[11 + i] << (8 * i);
    if (crc != mn_crc32(buf, 11)) return false;
    mn_cfg_t got = {
        .bpm = (uint16_t)(buf[3] | (buf[4] << 8)),
        .beats = buf[5], .accent = buf[6], .subdiv = buf[7], .sound = buf[8], .volume = buf[9],
    };
    if (!mn_cfg_sanitize(&got)) return false;
    *cfg = got;
    return true;
}

uint8_t mn_cfg_volume_percent(uint8_t level) {
    if (level == 0) return 0;
    if (level > MN_VOLUME_MAX) level = MN_VOLUME_MAX;
    return (uint8_t)(40u + 6u * level);   // 1 档 46% … 10 档 100%
}
