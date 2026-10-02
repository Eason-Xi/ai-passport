// main/yz_power.c —— 空闲调暗 / 熄屏。
#include "yz_power.h"

void yz_power_init(yz_power_t *p, uint32_t now_ms) {
    p->last_ms = now_ms;
    p->level = YZ_POWER_ON;
}

bool yz_power_key(yz_power_t *p, uint32_t now_ms) {
    const bool waking = p->level != YZ_POWER_ON;
    p->last_ms = now_ms;
    p->level = YZ_POWER_ON;
    return waking;
}

bool yz_power_tick(yz_power_t *p, uint32_t now_ms, bool busy) {
    if (busy) {
        p->last_ms = now_ms;
        if (p->level == YZ_POWER_ON) return false;
        p->level = YZ_POWER_ON;
        return true;
    }
    const uint32_t idle = now_ms - p->last_ms;   // 无符号减法可跨越计数回绕
    const uint8_t level = idle >= YZ_OFF_AFTER_MS ? YZ_POWER_OFF
                        : idle >= YZ_DIM_AFTER_MS ? YZ_POWER_DIM : YZ_POWER_ON;
    if (level == p->level) return false;
    p->level = level;
    return true;
}

uint8_t yz_power_backlight(const yz_power_t *p, const yz_cfg_t *cfg) {
    if (p->level == YZ_POWER_OFF) return 0;
    const uint8_t idx = cfg->bright_idx < YZ_BRIGHT_OPTIONS ? cfg->bright_idx : 2;
    const uint8_t full = YZ_BRIGHT_PERCENT[idx];
    if (p->level == YZ_POWER_DIM) return full < YZ_DIM_PERCENT ? full : YZ_DIM_PERCENT;
    return full;
}
