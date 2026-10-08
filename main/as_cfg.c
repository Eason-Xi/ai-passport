// main/as_cfg.c —— 设置默认值与存档编解码，说明见 as_cfg.h。
#include "as_cfg.h"

#include "as_crc32.h"

#define BODY_LEN 12

static const uint16_t TIMER_MIN[AS_TIMER_COUNT] = { 0, 15, 30, 45, 60, 90 };

void as_cfg_default(as_cfg_t *cfg) {
    // 首次开机：细雨为主，远处一点篝火；音量居中，30 分钟后渐弱停止。
    *cfg = (as_cfg_t){
        .sound = { AS_SOUND_RAIN, AS_SOUND_FIRE, AS_SOUND_NONE },
        .level = { 8, 4, 5 },
        .volume = 5,
        .timer = AS_TIMER_30,
        .breath = AS_BREATH_478,
    };
}

bool as_cfg_sanitize(as_cfg_t *cfg) {
    as_cfg_t def;
    as_cfg_default(&def);
    bool ok = true;
    for (int i = 0; i < AS_LAYERS; i++) {
        if (cfg->sound[i] >= AS_SOUND_COUNT && cfg->sound[i] != AS_SOUND_NONE) {
            cfg->sound[i] = def.sound[i];
            ok = false;
        }
        if (cfg->level[i] > AS_LEVEL_MAX) {
            cfg->level[i] = def.level[i];
            ok = false;
        }
    }
    if (cfg->volume < AS_VOLUME_MIN || cfg->volume > AS_VOLUME_MAX) { cfg->volume = def.volume; ok = false; }
    if (cfg->timer >= AS_TIMER_COUNT) { cfg->timer = def.timer; ok = false; }
    if (cfg->breath >= AS_BREATH_COUNT) { cfg->breath = def.breath; ok = false; }
    return ok;
}

size_t as_cfg_pack(const as_cfg_t *cfg, uint8_t *buf, size_t cap) {
    if (cap < AS_CFG_BLOB_SIZE) return 0;
    buf[0] = 'A';
    buf[1] = 'S';
    buf[2] = AS_CFG_VERSION;
    for (int i = 0; i < AS_LAYERS; i++) {
        buf[3 + i] = cfg->sound[i];
        buf[6 + i] = cfg->level[i];
    }
    buf[9] = cfg->volume;
    buf[10] = cfg->timer;
    buf[11] = cfg->breath;
    const uint32_t crc = as_crc32(buf, BODY_LEN);
    for (int i = 0; i < 4; i++) buf[BODY_LEN + i] = (uint8_t)(crc >> (8 * i));
    return AS_CFG_BLOB_SIZE;
}

bool as_cfg_unpack(as_cfg_t *cfg, const uint8_t *buf, size_t len) {
    as_cfg_default(cfg);
    if (len != AS_CFG_BLOB_SIZE || buf[0] != 'A' || buf[1] != 'S' || buf[2] != AS_CFG_VERSION) return false;
    uint32_t crc = 0;
    for (int i = 0; i < 4; i++) crc |= (uint32_t)buf[BODY_LEN + i] << (8 * i);
    if (crc != as_crc32(buf, BODY_LEN)) return false;
    as_cfg_t got;
    for (int i = 0; i < AS_LAYERS; i++) {
        got.sound[i] = buf[3 + i];
        got.level[i] = buf[6 + i];
    }
    got.volume = buf[9];
    got.timer = buf[10];
    got.breath = buf[11];
    if (!as_cfg_sanitize(&got)) return false;
    *cfg = got;
    return true;
}

uint8_t as_cfg_volume_percent(uint8_t volume) {
    if (volume < AS_VOLUME_MIN) volume = AS_VOLUME_MIN;
    if (volume > AS_VOLUME_MAX) volume = AS_VOLUME_MAX;
    return (uint8_t)(36u + 64u * volume / AS_VOLUME_MAX);   // 1 档 42%，10 档 100%
}

uint16_t as_timer_minutes(uint8_t opt) {
    return opt < AS_TIMER_COUNT ? TIMER_MIN[opt] : 0;
}

bool as_cfg_audible(const as_cfg_t *cfg) {
    for (int i = 0; i < AS_LAYERS; i++) {
        if (cfg->sound[i] != AS_SOUND_NONE && cfg->level[i] > 0) return true;
    }
    return false;
}
