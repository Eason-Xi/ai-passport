// main/as_model.c —— 应用状态机，说明见 as_model.h。
#include "as_model.h"

#include <string.h>

static bool reached(uint32_t now, uint32_t when) {
    return (int32_t)(now - when) >= 0;
}

uint8_t as_pick_to_sound(uint8_t index) {
    return index < AS_SOUND_COUNT ? index : AS_SOUND_NONE;
}

uint8_t as_sound_to_pick(uint8_t sound) {
    return sound < AS_SOUND_COUNT ? sound : AS_PICK_NONE_INDEX;
}

static void timer_reset(as_model_t *m) {
    m->timer_armed = m->cfg.timer != AS_TIMER_OFF;
    m->timer_left_ms = (uint32_t)as_timer_minutes(m->cfg.timer) * 60000u;
}

uint32_t as_model_timer_left(const as_model_t *m, uint32_t now_ms) {
    if (!m->timer_armed) return 0;
    if (!m->playing) return m->timer_left_ms;
    const int32_t rem = (int32_t)(m->timer_end_ms - now_ms);
    return rem > 0 ? (uint32_t)rem : 0;
}

bool as_model_keep_screen(const as_model_t *m, uint32_t now_ms) {
    if (m->night) return true;
    return m->page == AS_PAGE_BREATH && now_ms - m->breath_start_ms < AS_BREATH_KEEP_MS;
}

void as_model_init(as_model_t *m, const as_cfg_t *cfg, uint32_t now_ms) {
    memset(m, 0, sizeof *m);
    m->cfg = *cfg;
    as_cfg_sanitize(&m->cfg);
    m->page = AS_PAGE_HOME;
    m->fade = 32768;
    m->hold_btn = -1;
    m->breath_start_ms = now_ms;
    timer_reset(m);
    m->shown_left_s = m->timer_left_ms / 1000u;
}

static as_fx_t go(as_model_t *m, as_page_t page) {
    m->page = (uint8_t)page;
    m->hold_btn = -1;
    return AS_FX_SCREEN;
}

static as_fx_t play(as_model_t *m, uint32_t now) {
    if (!as_cfg_audible(&m->cfg)) {
        // 没有任何可听的层：直接带用户去调音台。
        m->mix_sel = 0;
        m->mix_edit = false;
        return go(m, AS_PAGE_MIXER);
    }
    m->playing = true;
    m->fade = 32768;
    if (!m->timer_armed) timer_reset(m);
    if (m->timer_armed) m->timer_end_ms = now + m->timer_left_ms;
    return AS_FX_AUDIO | AS_FX_REFRESH;
}

static as_fx_t pause(as_model_t *m, uint32_t now) {
    if (m->timer_armed) m->timer_left_ms = as_model_timer_left(m, now);
    m->playing = false;
    return AS_FX_AUDIO | AS_FX_REFRESH;
}

static as_fx_t volume_step(as_model_t *m, int dir, uint32_t now) {
    const int v = m->cfg.volume + dir;
    as_fx_t fx = AS_FX_REFRESH;
    if (v >= AS_VOLUME_MIN && v <= AS_VOLUME_MAX) {
        m->cfg.volume = (uint8_t)v;
        fx |= AS_FX_VOLUME | AS_FX_SAVE;
    }
    m->volume_shown = true;
    m->volume_until_ms = now + AS_VOLUME_OVERLAY_MS;
    return fx;
}

static as_fx_t level_step(as_model_t *m, int dir) {
    uint8_t *lv = &m->cfg.level[m->mix_sel];
    const int v = *lv + dir;
    if (v < 0 || v > AS_LEVEL_MAX) return AS_FX_NONE;
    *lv = (uint8_t)v;
    return AS_FX_AUDIO | AS_FX_SAVE | AS_FX_REFRESH;
}

// 当前页面上 ▲▼ 是否支持按住连调，以及每一步做什么。
static bool repeatable(const as_model_t *m) {
    return m->page == AS_PAGE_HOME || (m->page == AS_PAGE_MIXER && m->mix_edit);
}

static as_fx_t repeat_step(as_model_t *m, as_btn_t btn, uint32_t now) {
    const int dir = btn == AS_BTN_UP ? 1 : -1;
    if (m->page == AS_PAGE_HOME) return volume_step(m, dir, now);
    return level_step(m, dir);
}

static as_fx_t home_ok(as_model_t *m, as_ev_t ev, uint32_t now) {
    switch (ev) {
    case AS_EV_CLICK: return m->playing ? pause(m, now) : play(m, now);
    case AS_EV_DOUBLE:
        m->breath_start_ms = now;
        return go(m, AS_PAGE_BREATH);
    case AS_EV_LONG:
        m->menu_sel = AS_MENU_MIXER;
        return go(m, AS_PAGE_MENU);
    default: return AS_FX_NONE;
    }
}

static as_fx_t menu_ok(as_model_t *m, uint32_t now) {
    switch (m->menu_sel) {
    case AS_MENU_MIXER:
        m->mix_sel = 0;
        m->mix_edit = false;
        return go(m, AS_PAGE_MIXER);
    case AS_MENU_TIMER:
        m->timer_sel = m->cfg.timer;
        return go(m, AS_PAGE_TIMER);
    case AS_MENU_BREATH:
        m->breath_start_ms = now;
        return go(m, AS_PAGE_BREATH);
    default: return go(m, AS_PAGE_HOME);
    }
}

static as_fx_t picker_confirm(as_model_t *m) {
    const uint8_t sound = as_pick_to_sound(m->pick_sel);
    m->cfg.sound[m->mix_sel] = sound;
    if (sound != AS_SOUND_NONE && m->cfg.level[m->mix_sel] == 0) m->cfg.level[m->mix_sel] = 5;
    m->mix_edit = sound != AS_SOUND_NONE;   // 选好声音后直接调这一层的音量
    return go(m, AS_PAGE_MIXER) | AS_FX_AUDIO | AS_FX_SAVE;
}

static as_fx_t timer_confirm(as_model_t *m, uint32_t now) {
    as_fx_t fx = AS_FX_NONE;
    if (m->timer_sel != m->cfg.timer) fx |= AS_FX_SAVE;
    m->cfg.timer = m->timer_sel;
    // 重新选择定时：从现在起重新倒数，并取消已经开始的渐弱。
    timer_reset(m);
    if (m->playing && m->timer_armed) m->timer_end_ms = now + m->timer_left_ms;
    if (m->fade != 32768) {
        m->fade = 32768;
        fx |= AS_FX_AUDIO;
    }
    m->shown_left_s = as_model_timer_left(m, now) / 1000u;
    return fx | go(m, AS_PAGE_HOME);
}

static int wrap(int v, int count) {
    return (v % count + count) % count;
}

as_fx_t as_model_key(as_model_t *m, as_btn_t btn, as_ev_t ev, uint32_t now) {
    if (m->night) {
        // "晚安"画面上任意键回到聆听页（按键本身不再触发其他功能）。
        if (ev != AS_EV_PRESS) return AS_FX_NONE;
        m->night = false;
        return go(m, AS_PAGE_HOME);
    }
    if (btn != AS_BTN_OK) {
        const int dir = btn == AS_BTN_UP ? -1 : 1;   // 列表里 ▲ 向上（索引减小）
        if (ev == AS_EV_RELEASE) {
            if (m->hold_btn == (int8_t)btn) m->hold_btn = -1;
            return AS_FX_NONE;
        }
        if (ev == AS_EV_LONG && repeatable(m)) {
            m->hold_btn = (int8_t)btn;
            m->hold_next_ms = now + AS_HOLD_REPEAT_MS;
            return repeat_step(m, btn, now);
        }
        if (ev != AS_EV_PRESS) return AS_FX_NONE;
        m->hold_btn = -1;
        switch (m->page) {
        case AS_PAGE_HOME:
            return repeat_step(m, btn, now);
        case AS_PAGE_MENU:
            m->menu_sel = (uint8_t)wrap(m->menu_sel + dir, AS_MENU_COUNT);
            return AS_FX_REFRESH;
        case AS_PAGE_MIXER:
            if (m->mix_edit) return repeat_step(m, btn, now);
            m->mix_sel = (uint8_t)wrap(m->mix_sel + dir, AS_LAYERS + 1);
            return AS_FX_REFRESH;
        case AS_PAGE_PICKER:
            m->pick_sel = (uint8_t)wrap(m->pick_sel + dir, AS_PICK_COUNT);
            m->cfg.sound[m->mix_sel] = as_pick_to_sound(m->pick_sel);   // 实时试听
            return AS_FX_AUDIO | AS_FX_REFRESH;
        case AS_PAGE_TIMER: {
            const int v = m->timer_sel + dir;
            if (v < 0 || v >= AS_TIMER_COUNT) return AS_FX_NONE;
            m->timer_sel = (uint8_t)v;
            return AS_FX_REFRESH;
        }
        case AS_PAGE_BREATH:
            m->cfg.breath = (uint8_t)wrap(m->cfg.breath + (btn == AS_BTN_UP ? 1 : -1), AS_BREATH_COUNT);
            m->breath_start_ms = now;
            return AS_FX_SAVE | AS_FX_REFRESH;
        default: return AS_FX_NONE;
        }
    }

    // ---- OK 键 ----
    if (ev == AS_EV_PRESS || ev == AS_EV_RELEASE) return AS_FX_NONE;
    const bool back = ev == AS_EV_LONG;
    switch (m->page) {
    case AS_PAGE_HOME: return home_ok(m, ev, now);
    case AS_PAGE_MENU: return back ? go(m, AS_PAGE_HOME) : menu_ok(m, now);
    case AS_PAGE_MIXER:
        if (m->mix_edit) {
            m->mix_edit = false;
            m->hold_btn = -1;
            return AS_FX_REFRESH;
        }
        if (back || m->mix_sel == AS_MIX_DONE) return go(m, AS_PAGE_HOME);
        m->pick_orig = m->cfg.sound[m->mix_sel];
        m->pick_sel = as_sound_to_pick(m->pick_orig);
        return go(m, AS_PAGE_PICKER);
    case AS_PAGE_PICKER:
        if (back) {
            m->cfg.sound[m->mix_sel] = m->pick_orig;
            return go(m, AS_PAGE_MIXER) | AS_FX_AUDIO;
        }
        return picker_confirm(m);
    case AS_PAGE_TIMER: return back ? go(m, AS_PAGE_HOME) : timer_confirm(m, now);
    case AS_PAGE_BREATH: return go(m, AS_PAGE_HOME);
    default: return AS_FX_NONE;
    }
}

as_fx_t as_model_tick(as_model_t *m, uint32_t now) {
    as_fx_t fx = AS_FX_NONE;

    if (m->hold_btn >= 0 && reached(now, m->hold_next_ms)) {
        if (repeatable(m)) {
            fx |= repeat_step(m, (as_btn_t)m->hold_btn, now);
            m->hold_next_ms += AS_HOLD_REPEAT_MS;
            if (!reached(m->hold_next_ms, now)) m->hold_next_ms = now + AS_HOLD_REPEAT_MS;
        } else {
            m->hold_btn = -1;
        }
    }

    if (m->volume_shown && reached(now, m->volume_until_ms)) {
        m->volume_shown = false;
        fx |= AS_FX_REFRESH;
    }

    if (m->playing && m->timer_armed) {
        const uint32_t left = as_model_timer_left(m, now);
        if (left == 0) {
            // 到点：此时渐弱增益已为 0，停止播放后保持 0，下次开始播放再恢复。
            m->playing = false;
            m->fade = 0;
            timer_reset(m);
            m->night = true;
            m->night_until_ms = now + AS_NIGHT_MS;
            m->shown_left_s = m->timer_left_ms / 1000u;
            return fx | AS_FX_AUDIO | AS_FX_NIGHT | go(m, AS_PAGE_HOME);
        }
        const int32_t g = as_timer_fade_gain(left);
        if (g != m->fade) {
            m->fade = g;
            fx |= AS_FX_AUDIO;
        }
        if (left / 1000u != m->shown_left_s) {
            m->shown_left_s = left / 1000u;
            fx |= AS_FX_REFRESH;
        }
    }

    if (m->night && reached(now, m->night_until_ms)) {
        m->night = false;
        fx |= AS_FX_NIGHT_END | AS_FX_SCREEN;
    }
    return fx;
}

uint32_t as_model_wait_ms(const as_model_t *m) {
    return m->hold_btn >= 0 ? AS_TICK_FAST_MS : AS_TICK_SLOW_MS;
}
