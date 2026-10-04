// main/mn_model.c —— 节拍器应用状态机，说明见 mn_model.h。
#include "mn_model.h"

#include <string.h>

#include "mn_sound.h"
#include "mn_tempo.h"

void mn_model_init(mn_model_t *m, const mn_cfg_t *cfg) {
    memset(m, 0, sizeof *m);
    m->cfg = *cfg;
    mn_cfg_sanitize(&m->cfg);
    m->page = MN_PAGE_MAIN;
}

static void stop_repeat(mn_model_t *m) {
    m->rep_active = false;
}

// ---- 设置值调整 ----

static uint8_t wrap(int v, int lo, int hi) {
    if (v < lo) return (uint8_t)hi;
    if (v > hi) return (uint8_t)lo;
    return (uint8_t)v;
}

static uint8_t clamp_u8(int v, int lo, int hi) {
    if (v < lo) return (uint8_t)lo;
    if (v > hi) return (uint8_t)hi;
    return (uint8_t)v;
}

// 主界面的一步 BPM 调整。
static mn_fx_t bump_bpm(mn_model_t *m, int dir, uint8_t step) {
    const uint16_t next = mn_bpm_bump(m->cfg.bpm, dir, step);
    if (next == m->cfg.bpm) return 0;
    m->cfg.bpm = next;
    return MN_FX_METER | MN_FX_SAVE | MN_FX_REFRESH;
}

// 设置页编辑态下当前行的值 ±1。
static mn_fx_t adjust_row(mn_model_t *m, int dir) {
    mn_cfg_t *c = &m->cfg;
    const mn_cfg_t before = *c;
    mn_fx_t fx = 0;
    switch (m->row) {
    case MN_ROW_BEATS:
        c->beats = clamp_u8(c->beats + dir, MN_BEATS_MIN, MN_BEATS_MAX);
        fx = MN_FX_METER;
        break;
    case MN_ROW_ACCENT:
        c->accent = !c->accent;
        fx = MN_FX_METER;
        break;
    case MN_ROW_SUBDIV:
        c->subdiv = wrap(c->subdiv + dir, 1, MN_SUBDIV_MAX);
        fx = MN_FX_METER;
        break;
    case MN_ROW_SOUND:
        c->sound = wrap(c->sound + dir, 0, MN_SOUND_COUNT - 1);
        fx = MN_FX_SOUND;
        break;
    case MN_ROW_VOLUME:
        c->volume = clamp_u8(c->volume + dir, 0, MN_VOLUME_MAX);
        fx = MN_FX_VOLUME;
        break;
    default:
        return 0;
    }
    if (memcmp(&before, c, sizeof before) == 0) return 0;
    return fx | MN_FX_SAVE | MN_FX_REFRESH;
}

static bool row_repeats(uint8_t row) {
    return row == MN_ROW_BEATS || row == MN_ROW_VOLUME;
}

// 长按连调的一次自动步进（第 n 次）。
static mn_fx_t repeat_step(mn_model_t *m, uint32_t n) {
    if (m->page == MN_PAGE_MAIN) return bump_bpm(m, m->rep_dir, mn_repeat_step(n));
    if (m->page == MN_PAGE_SETTINGS && m->editing && row_repeats(m->row)) return adjust_row(m, m->rep_dir);
    stop_repeat(m);
    return 0;
}

static mn_fx_t start_repeat(mn_model_t *m, mn_btn_t btn, uint32_t now_ms) {
    m->rep_active = true;
    m->rep_btn = (uint8_t)btn;
    m->rep_dir = btn == MN_BTN_UP ? 1 : -1;
    m->rep_start_ms = now_ms;
    const mn_fx_t fx = repeat_step(m, 0);
    m->rep_n = 1;
    m->rep_due_ms = now_ms + mn_repeat_delay_ms(0);
    return fx;
}

// ---- 敲击测速 ----

static mn_fx_t enter_tap(mn_model_t *m, uint32_t now_ms) {
    mn_fx_t fx = MN_FX_SCREEN;
    stop_repeat(m);
    m->tap_return = m->page;
    m->tap_resume = m->running;
    if (m->running) {
        m->running = false;
        fx |= MN_FX_RUN;
    }
    mn_tap_reset(&m->tap);
    m->tap_done = false;
    m->tap_result = 0;
    m->tap_enter_ms = now_ms;
    m->tap_last_ms = now_ms;
    m->editing = false;
    m->page = MN_PAGE_TAP;
    return fx;
}

static mn_fx_t leave_tap(mn_model_t *m) {
    mn_fx_t fx = MN_FX_SCREEN;
    m->page = m->tap_return;
    m->tap_done = false;
    if (m->tap_resume) {
        m->running = true;
        fx |= MN_FX_RUN;
    }
    return fx;
}

// ---- 按键 ----

static mn_fx_t key_main(mn_model_t *m, mn_btn_t btn, mn_ev_t ev, uint32_t now_ms) {
    if (btn == MN_BTN_UP || btn == MN_BTN_DOWN) {
        if (ev == MN_EV_PRESS) return bump_bpm(m, btn == MN_BTN_UP ? 1 : -1, 1);
        if (ev == MN_EV_LONG) return start_repeat(m, btn, now_ms);
        return 0;
    }
    switch (ev) {
    case MN_EV_CLICK:
        m->running = !m->running;
        return MN_FX_RUN | MN_FX_REFRESH;
    case MN_EV_DOUBLE:
        return enter_tap(m, now_ms);
    case MN_EV_LONG:
        stop_repeat(m);
        m->page = MN_PAGE_SETTINGS;
        m->row = MN_ROW_BEATS;
        m->editing = false;
        return MN_FX_SCREEN;
    default:
        return 0;
    }
}

static mn_fx_t key_settings(mn_model_t *m, mn_btn_t btn, mn_ev_t ev, uint32_t now_ms) {
    if (btn == MN_BTN_OK) {
        if (ev == MN_EV_LONG) {
            stop_repeat(m);
            m->editing = false;
            m->page = MN_PAGE_MAIN;
            return MN_FX_SCREEN;
        }
        if (ev != MN_EV_CLICK) return 0;
        if (m->editing) {
            m->editing = false;
            return MN_FX_REFRESH;
        }
        if (m->row == MN_ROW_TAP) return enter_tap(m, now_ms);
        m->editing = true;
        return MN_FX_REFRESH;
    }
    const int dir = btn == MN_BTN_UP ? 1 : -1;
    if (!m->editing) {
        if (ev != MN_EV_PRESS) return 0;
        // 列表方向：UP 向上（上一行），DOWN 向下，首尾循环。
        m->row = (uint8_t)((m->row + MN_ROW_COUNT - dir) % MN_ROW_COUNT);
        return MN_FX_REFRESH;
    }
    if (ev == MN_EV_PRESS) return adjust_row(m, dir);
    if (ev == MN_EV_LONG && row_repeats(m->row)) return start_repeat(m, btn, now_ms);
    return 0;
}

static mn_fx_t key_tap(mn_model_t *m, mn_btn_t btn, mn_ev_t ev, uint32_t now_ms, int64_t t_us) {
    if (btn == MN_BTN_OK && ev == MN_EV_LONG) return leave_tap(m);   // 取消，不改 BPM
    if (ev != MN_EV_PRESS || m->tap_done) return 0;
    if (!mn_tap_add(&m->tap, t_us)) return 0;
    m->tap_last_ms = now_ms;
    return MN_FX_REFRESH | MN_FX_TAP_FLASH;
}

mn_fx_t mn_model_key(mn_model_t *m, mn_btn_t btn, mn_ev_t ev, uint32_t now_ms, int64_t t_us) {
    if (btn >= MN_BTN_COUNT) return 0;
    if (ev == MN_EV_RELEASE) {
        if (m->rep_active && m->rep_btn == btn) stop_repeat(m);
        return 0;
    }
    switch (m->page) {
    case MN_PAGE_MAIN:
        return key_main(m, btn, ev, now_ms);
    case MN_PAGE_SETTINGS:
        return key_settings(m, btn, ev, now_ms);
    case MN_PAGE_TAP:
        return key_tap(m, btn, ev, now_ms, t_us);
    }
    return 0;
}

// ---- 周期 ----

mn_fx_t mn_model_tick(mn_model_t *m, uint32_t now_ms) {
    mn_fx_t fx = 0;
    if (m->rep_active) {
        if (now_ms - m->rep_start_ms >= MN_REPEAT_GUARD_MS) {
            stop_repeat(m);
        } else {
            // 应用任务偶尔被耽搁时最多补一步，不要一次跳很多。
            if ((int32_t)(now_ms - m->rep_due_ms) > 200) m->rep_due_ms = now_ms;
            while (m->rep_active && (int32_t)(now_ms - m->rep_due_ms) >= 0) {
                fx |= repeat_step(m, m->rep_n);
                m->rep_due_ms += mn_repeat_delay_ms(m->rep_n);
                m->rep_n++;
            }
        }
    }
    if (m->page == MN_PAGE_TAP) {
        if (m->tap_done) {
            if (now_ms - m->tap_done_ms >= MN_TAP_DONE_SHOW_MS) fx |= leave_tap(m);
        } else if (mn_tap_count(&m->tap) >= MN_TAP_MIN_TAPS && mn_tap_bpm(&m->tap) &&
                   now_ms - m->tap_last_ms >= MN_TAP_APPLY_IDLE_MS) {
            m->tap_result = mn_tap_bpm(&m->tap);
            m->tap_done = true;
            m->tap_done_ms = now_ms;
            fx |= MN_FX_REFRESH;
            if (m->tap_result != m->cfg.bpm) {
                m->cfg.bpm = m->tap_result;
                fx |= MN_FX_METER | MN_FX_SAVE;
            }
        } else if (now_ms - m->tap_last_ms >= MN_TAP_GIVEUP_MS) {
            fx |= leave_tap(m);
        }
    }
    return fx;
}

static uint32_t until(uint32_t now_ms, uint32_t due_ms) {
    const int32_t d = (int32_t)(due_ms - now_ms);
    return d > 0 ? (uint32_t)d : 0;
}

uint32_t mn_model_wait_ms(const mn_model_t *m, uint32_t now_ms, uint32_t max_ms) {
    uint32_t wait = max_ms;
    if (m->rep_active) {
        const uint32_t w = until(now_ms, m->rep_due_ms);
        if (w < wait) wait = w;
    }
    if (m->page == MN_PAGE_TAP) {
        uint32_t due;
        if (m->tap_done) due = m->tap_done_ms + MN_TAP_DONE_SHOW_MS;
        else if (mn_tap_count(&m->tap) >= MN_TAP_MIN_TAPS) due = m->tap_last_ms + MN_TAP_APPLY_IDLE_MS;
        else due = m->tap_last_ms + MN_TAP_GIVEUP_MS;
        const uint32_t w = until(now_ms, due);
        if (w < wait) wait = w;
    }
    return wait;
}

// ---- 背光 ----

void mn_power_init(mn_power_t *p, uint32_t now_ms) {
    memset(p, 0, sizeof *p);
    p->last_ms = now_ms;
    p->wake_btn = -1;
}

bool mn_power_key(mn_power_t *p, mn_btn_t btn, mn_ev_t ev, uint32_t now_ms) {
    if (p->dimmed) {
        p->last_ms = now_ms;
        if (ev != MN_EV_PRESS) return true;   // 熄屏期间只认按下来唤醒
        p->dimmed = false;
        p->wake_btn = (int8_t)btn;
        p->wake_released = false;
        return true;
    }
    if (p->wake_btn >= 0) {
        if ((int8_t)btn == p->wake_btn) {
            if (!p->wake_released) {
                if (ev == MN_EV_RELEASE) {
                    p->wake_released = true;
                    p->release_ms = now_ms;
                }
                p->last_ms = now_ms;
                return true;   // 唤醒键按住期间的 LONG / RELEASE 都吞掉
            }
            if ((ev == MN_EV_CLICK || ev == MN_EV_DOUBLE) && now_ms - p->release_ms <= MN_WAKE_SWALLOW_MS) {
                return true;   // 松开后延迟到达的单击 / 双击
            }
        }
        p->wake_btn = -1;
    }
    p->last_ms = now_ms;
    return false;
}

bool mn_power_tick(mn_power_t *p, uint32_t now_ms, bool busy) {
    if (busy) {
        p->last_ms = now_ms;
        return false;
    }
    if (!p->dimmed && now_ms - p->last_ms >= MN_DIM_AFTER_MS) {
        p->dimmed = true;
        return true;
    }
    return false;
}

uint8_t mn_power_backlight(const mn_power_t *p) {
    return p->dimmed ? MN_BACKLIGHT_DIM : MN_BACKLIGHT_ON;
}
