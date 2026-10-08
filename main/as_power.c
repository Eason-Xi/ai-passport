// main/as_power.c —— 省电与低电量判定，说明见 as_power.h。
#include "as_power.h"

#include <string.h>

// 与 BSP / as_model 一致的事件编号（此文件不依赖 BSP 头，便于主机测试）。
enum { EV_PRESS = 0, EV_CLICK = 1, EV_DOUBLE = 2, EV_LONG = 3, EV_RELEASE = 4 };

void as_power_init(as_power_t *p, uint32_t now_ms) {
    memset(p, 0, sizeof *p);
    p->last_ms = now_ms;
    p->level = AS_POWER_ON;
    p->wake_btn = -1;
}

bool as_power_key(as_power_t *p, uint8_t btn, uint8_t ev, uint32_t now_ms) {
    if (p->level != AS_POWER_ON) {
        p->last_ms = now_ms;
        if (ev != EV_PRESS) return true;   // 暗屏 / 熄屏期间只认"按下"来唤醒
        p->level = AS_POWER_ON;
        p->wake_btn = (int8_t)btn;
        p->wake_released = false;
        return true;
    }
    if (p->wake_btn >= 0) {
        if ((int8_t)btn == p->wake_btn) {
            if (!p->wake_released) {
                if (ev == EV_RELEASE) {
                    p->wake_released = true;
                    p->release_ms = now_ms;
                }
                p->last_ms = now_ms;
                return true;   // 唤醒键按住期间的 LONG / RELEASE 都吞掉
            }
            if ((ev == EV_CLICK || ev == EV_DOUBLE) && now_ms - p->release_ms <= AS_WAKE_SWALLOW_MS) {
                return true;   // 松开后延迟到达的单击 / 双击
            }
        }
        p->wake_btn = -1;
    }
    p->last_ms = now_ms;
    return false;
}

bool as_power_tick(as_power_t *p, uint32_t now_ms, as_activity_t act) {
    if (act == AS_ACT_KEEP_ON) {
        p->last_ms = now_ms;
        return false;
    }
    const uint32_t idle = now_ms - p->last_ms;
    uint8_t target = AS_POWER_ON;
    if (act == AS_ACT_PLAYING) {
        if (idle >= AS_PLAY_OFF_MS) target = AS_POWER_SCREEN_OFF;
        else if (idle >= AS_PLAY_DIM_MS) target = AS_POWER_DIM;
    } else {
        if (idle >= AS_IDLE_SHUTDOWN_MS) target = AS_POWER_SHUTDOWN;
        else if (idle >= AS_IDLE_OFF_MS) target = AS_POWER_SCREEN_OFF;
        else if (idle >= AS_IDLE_DIM_MS) target = AS_POWER_DIM;
    }
    if (target <= p->level) return false;
    p->level = target;
    return true;
}

void as_power_wake(as_power_t *p, uint32_t now_ms) {
    p->level = AS_POWER_ON;
    p->last_ms = now_ms;
    p->wake_btn = -1;
}

void as_power_force(as_power_t *p, uint8_t level, uint32_t now_ms) {
    p->level = level;
    p->last_ms = now_ms;
    p->wake_btn = -1;
}

uint8_t as_power_backlight(const as_power_t *p) {
    switch (p->level) {
    case AS_POWER_ON: return AS_BACKLIGHT_ON;
    case AS_POWER_DIM: return AS_BACKLIGHT_DIM;
    default: return AS_BACKLIGHT_OFF;
    }
}

void as_lowbatt_init(as_lowbatt_t *b) {
    b->count = 0;
    b->first_mv = -1;
}

bool as_lowbatt_feed(as_lowbatt_t *b, int soc, int mv, bool usb_host) {
    if (usb_host || soc < 0 || soc > AS_LOWBATT_SOC) {
        as_lowbatt_init(b);
        return false;
    }
    if (b->count == 0) b->first_mv = mv;
    if (mv >= 0 && b->first_mv >= 0 && mv > b->first_mv + AS_LOWBATT_RISE_MV) {
        as_lowbatt_init(b);   // 电压在上升：多半插着充电头
        return false;
    }
    if (b->count < 255) b->count++;
    return b->count >= AS_LOWBATT_SAMPLES;
}
