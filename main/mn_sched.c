// main/mn_sched.c —— 采样级节拍调度与单声部混音，说明见 mn_sched.h。
#include "mn_sched.h"

#include <string.h>

#define SAMPLES_PER_MINUTE ((uint32_t)MN_SAMPLE_RATE * 60u)

mn_meter_t mn_meter_sanitize(mn_meter_t m) {
    if (m.bpm < MN_BPM_MIN) m.bpm = MN_BPM_MIN;
    if (m.bpm > MN_BPM_MAX) m.bpm = MN_BPM_MAX;
    if (m.beats < MN_BEATS_MIN) m.beats = MN_BEATS_MIN;
    if (m.beats > MN_BEATS_MAX) m.beats = MN_BEATS_MAX;
    if (m.subdiv < 1) m.subdiv = 1;
    if (m.subdiv > MN_SUBDIV_MAX) m.subdiv = MN_SUBDIV_MAX;
    return m;
}

// 按当前 BPM 与本拍细分数重算间隔，并清零余数累加器（最多引入 < 1 个样本的相位误差）。
static void compute_step(mn_sched_t *s) {
    s->step_den = (uint32_t)s->cur.bpm * s->sub_active;
    s->step_whole = SAMPLES_PER_MINUTE / s->step_den;
    s->step_rem = SAMPLES_PER_MINUTE % s->step_den;
    s->err = 0;
}

uint64_t mn_sched_ideal_pos(uint64_t k, uint16_t bpm, uint8_t subdiv) {
    return k * SAMPLES_PER_MINUTE / ((uint64_t)bpm * subdiv);
}

void mn_sched_start(mn_sched_t *s, const mn_meter_t *meter) {
    memset(s, 0, sizeof *s);
    s->cur = mn_meter_sanitize(*meter);
    s->sub_active = s->cur.subdiv;
    compute_step(s);
}

void mn_sched_set(mn_sched_t *s, const mn_meter_t *meter) {
    const mn_meter_t m = mn_meter_sanitize(*meter);
    const bool bpm_changed = m.bpm != s->cur.bpm;
    s->cur = m;
    if (!bpm_changed) return;
    compute_step(s);
    if (s->has_last) {
        // 从上一个 tick 起按新间隔重排；加速过猛导致"已经过期"时，在当前块开头补响。
        const uint64_t next = s->last_tick + s->step_whole;
        s->next_tick = next > s->pos ? next : s->pos;
    }
}

static uint8_t tick_kind(const mn_sched_t *s) {
    if (s->sub != 0) return MN_TICK_SUB;
    if (s->beat == 0 && s->cur.accent && s->cur.beats > 1) return MN_TICK_ACCENT;
    return MN_TICK_BEAT;
}

size_t mn_sched_advance(mn_sched_t *s, uint32_t n, mn_tick_t *out, size_t max) {
    const uint64_t end = s->pos + n;
    size_t count = 0;
    while (s->next_tick < end) {
        if (s->sub == 0) {
            // 拍头：细分与拍数的变更在这里生效。
            if (s->beat >= s->cur.beats) s->beat = 0;
            if (s->sub_active != s->cur.subdiv) {
                s->sub_active = s->cur.subdiv;
                compute_step(s);
            }
        }
        if (count < max) {
            out[count++] = (mn_tick_t){
                .offset = (uint32_t)(s->next_tick - s->pos),
                .kind = tick_kind(s),
                .beat = s->beat,
                .sub = s->sub,
                .subdiv = s->sub_active,
                .beats = s->cur.beats,
                .bpm = s->cur.bpm,
                .beat_no = s->beat_no,
            };
        }
        s->last_tick = s->next_tick;
        s->has_last = true;
        s->next_tick += s->step_whole;
        s->err += s->step_rem;
        if (s->err >= s->step_den) {
            s->err -= s->step_den;
            s->next_tick++;
        }
        if (++s->sub >= s->sub_active) {
            s->sub = 0;
            s->beat_no++;
            if (++s->beat >= s->cur.beats) s->beat = 0;
        }
    }
    s->pos = end;
    return count;
}

void mn_voice_reset(mn_voice_t *v) {
    memset(v, 0, sizeof *v);
}

bool mn_voice_active(const mn_voice_t *v) {
    return v->pcm && v->idx < v->len;
}

// 把正在播放的 click 写入 out[from, to)，之后的位置保持 0。
static void play_span(mn_voice_t *v, int16_t *out, uint32_t from, uint32_t to) {
    for (uint32_t i = from; i < to && mn_voice_active(v); i++) out[i] = v->pcm[v->idx++];
}

void mn_render(mn_sched_t *sched, mn_voice_t *voice, const mn_bank_t *bank, bool mute,
               int16_t *out, uint32_t n, mn_tick_t *ticks, size_t max, size_t *count) {
    memset(out, 0, (size_t)n * sizeof *out);
    const size_t got = sched ? mn_sched_advance(sched, n, ticks, max) : 0;
    if (count) *count = got;
    if (mute || !bank) {
        mn_voice_reset(voice);
        return;
    }
    uint32_t cursor = 0;
    for (size_t i = 0; i < got; i++) {
        play_span(voice, out, cursor, ticks[i].offset);
        cursor = ticks[i].offset;
        voice->pcm = bank->pcm[ticks[i].kind];
        voice->len = bank->len[ticks[i].kind];
        voice->idx = 0;
    }
    play_span(voice, out, cursor, n);
}
