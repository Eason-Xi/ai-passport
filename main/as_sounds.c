// main/as_sounds.c —— 声景合成器，说明见 as_sounds.h。
//
// 约定：慢变化参数（包络、扫频、漂移）每 16 个样本更新一次（SLOW_MASK），
// 其余逐样本计算。各声景末尾的 G_* 增益按 tools/asmr_audio_preview.c 渲染的 60 s 样本校准：
// 先去掉 300 Hz 以下（小喇叭放不出），再按 A 计权 RMS 对齐到约 −21 dBFS，使不同声景在
// 同一层音量下听感接近；篝火（−24）、虫鸣（−24）刻意轻一些，颂钵按峰值 ≤ 30000 取值。
#include "as_sounds.h"

#include <string.h>

#define SLOW_MASK 15u
#define MS(ms) ((uint32_t)(ms) * (AS_SAMPLE_RATE / 1000))

// 各声景的输出增益（Q15 × 倍数，见文件头说明）。
#define G_RAIN 15170
#define G_WAVES 15080
#define G_FIRE 24100
#define G_STREAM 10730
#define G_WIND 22400
#define G_CRICKETS 17660
#define G_BOWL 33900
#define G_WHITE 4960
#define G_PINK 8560
#define G_BROWN 13440

static const char *const NAMES[AS_SOUND_COUNT] = {
    "rain", "waves", "fire", "stream", "wind", "crickets", "bowl", "white", "pink", "brown",
};

const char *as_sound_debug_name(uint8_t id) {
    return id < AS_SOUND_COUNT ? NAMES[id] : "none";
}

// ---- 公共小工具 ----

// 偏向小值的随机幅度（平方分布）：大量轻响、偶尔重响，更像自然声。
static int32_t rand_amp(as_rng_t *r, int32_t lo, int32_t hi) {
    const int32_t u = as_rng_range(r, 0, 32767);
    return lo + (int32_t)(((int64_t)(hi - lo) * u / 32768) * u / 32768);
}

// 衰减到约 −60 dB 视为空闲，可复用。
static bool partial_idle(const as_partial_t *p) {
    return p->env < (AS_Q30 >> 10);
}

// 在声部池里找一个空闲槽；都在响就抢包络最小的那个。
static as_partial_t *grab(as_partial_t *pool, int n) {
    as_partial_t *best = &pool[0];
    for (int i = 0; i < n; i++) {
        if (partial_idle(&pool[i])) return &pool[i];
        if (pool[i].env < best->env) best = &pool[i];
    }
    return best;
}

// 噪声颗粒：样本 = 噪声 × 幅度 × 包络。
static int32_t grain_noise(as_partial_t *p, int32_t noise) {
    if (partial_idle(p)) return 0;
    const int32_t s = as_mul_q15(as_mul_q15(noise, p->amp), p->env >> 15);
    p->env = as_decay(p->env, p->k);
    return s;
}

// 正弦颗粒（可带滑音）。
static int32_t grain_sine(as_partial_t *p) {
    if (partial_idle(p)) return 0;
    const int32_t s = as_mul_q15(as_mul_q15(as_sine(p->phase), p->amp), p->env >> 15);
    p->phase += p->inc;
    p->inc = (uint32_t)((int32_t)p->inc + p->inc_slope);
    p->env = as_decay(p->env, p->k);
    return s;
}

// ---- 细雨 ----

static void rain_init(as_voice_t *v) {
    as_pink_init(&v->u.rain.pink);
    as_lp1_init(&v->u.rain.hp, 450);
    as_lp1_init(&v->u.rain.lp, 5500);
    as_lp1_init(&v->u.rain.drop_lp, 3800);
    as_drift_init(&v->u.rain.intensity, 17000, 32767, 3, 26000);
}

static int32_t rain_sample(as_voice_t *v) {
    as_rng_t *r = &v->rng;
    const int32_t intensity = v->u.rain.intensity.value;
    if ((v->t & SLOW_MASK) == 0) as_drift(&v->u.rain.intensity, r);

    // 雨幕：去掉低频的粉噪声，再柔化高频。
    const int32_t w = as_rng_s16(r);
    int32_t bed = as_lp1(&v->u.rain.lp, as_hp1(&v->u.rain.hp, as_pink(&v->u.rain.pink, w)));
    bed = as_mul_q15(bed, intensity);

    // 雨滴：每秒约 25–75 滴，随雨势变化；约 1/7 是落进水洼的"叮"声。
    const uint32_t rate = 25u + (uint32_t)(50 * intensity / 32768);
    if (as_rng_chance(r, rate, AS_SAMPLE_RATE)) {
        as_partial_t *d = grab(v->u.rain.drops, AS_RAIN_DROPS);
        d->env = AS_Q30 - 1;
        if (as_rng_chance(r, 1, 7)) {
            const int32_t hz = as_rng_range(r, 1400, 3200);
            d->inc = as_phase_inc_x16(hz * 16);
            d->inc_slope = (int32_t)(d->inc / 2400u);   // 小水泡：音高上滑
            d->k = as_decay_coef(as_rng_range(r, 12, 24));
            d->amp = rand_amp(r, 2000, 9000);
        } else {
            d->inc = 0;
            d->inc_slope = 0;
            d->k = as_decay_coef(as_rng_range(r, 2, 7));
            d->amp = rand_amp(r, 2500, 30000);
        }
    }
    int32_t drops = 0, plinks = 0;
    for (int i = 0; i < AS_RAIN_DROPS; i++) {
        as_partial_t *d = &v->u.rain.drops[i];
        if (partial_idle(d)) continue;   // 空闲颗粒不取随机数，省下大部分开销
        if (d->inc) plinks += grain_sine(d);
        else drops += grain_noise(d, as_rng_s16(r));
    }
    drops = as_lp1(&v->u.rain.drop_lp, drops);
    return as_mul_q15(bed + drops + plinks, G_RAIN);
}

// ---- 海浪 ----

#define WAVE_FLOOR 5200   // 两浪之间的底噪（Q15，约 0.16）

static void waves_next(as_voice_t *v) {
    as_rng_t *r = &v->rng;
    v->u.waves.len = MS(as_rng_range(r, 7000, 13000));
    v->u.waves.peak = v->u.waves.len * (uint32_t)as_rng_range(r, 32, 46) / 100u;
    v->u.waves.pos = 0;
    v->u.waves.height = as_rng_range(r, 19000, 32767);
}

static void waves_init(as_voice_t *v) {
    as_lp1_init(&v->u.waves.rumble_a, 260);
    as_lp1_init(&v->u.waves.rumble_b, 260);
    as_svf_init(&v->u.waves.body, 400, 7);
    as_lp1_init(&v->u.waves.hiss_hp, 2600);
    v->u.waves.env = WAVE_FLOOR;
    waves_next(v);
    // 第一浪从中途开始，避免启动后先安静好几秒。
    v->u.waves.pos = v->u.waves.peak / 3u;
}

// 每 16 个样本更新一次包络与音色。
static void waves_slow(as_voice_t *v) {
    const uint32_t pos = v->u.waves.pos, peak = v->u.waves.peak, len = v->u.waves.len;
    const int32_t span = v->u.waves.height - WAVE_FLOOR;
    int32_t shape;
    if (pos < peak) {
        const int32_t u = (int32_t)((uint64_t)pos * 32768u / peak);
        shape = (int32_t)((int64_t)u * u >> 15);                         // 涌起：越来越快
    } else {
        const int32_t u = (int32_t)((uint64_t)(pos - peak) * 32768u / (len - peak));
        const int32_t rest = 32768 - u;
        shape = (int32_t)((int64_t)rest * rest >> 15);                   // 拍岸后退去：先快后慢
    }
    v->u.waves.env = WAVE_FLOOR + (int32_t)((int64_t)span * shape >> 15);
    // 浪越高，水声越亮。
    as_svf_set(&v->u.waves.body, 260 + (int32_t)(1500 * (int64_t)v->u.waves.env / 32768), 7);
    v->u.waves.pos += SLOW_MASK + 1;
    if (v->u.waves.pos >= len) waves_next(v);
}

static int32_t waves_sample(as_voice_t *v) {
    if ((v->t & SLOW_MASK) == 0) waves_slow(v);
    const int32_t env = v->u.waves.env;
    const int32_t w = as_rng_s16(&v->rng);
    int32_t low;
    as_svf(&v->u.waves.body, w, &low, NULL);
    // 浪花泡沫：只在浪峰附近明显（包络三次方）。
    const int32_t crest = (int32_t)((int64_t)env * env >> 15) * (int64_t)env >> 15;
    const int32_t foam = as_mul_q15(as_hp1(&v->u.waves.hiss_hp, w), crest) / 5;
    const int32_t rumble = as_lp1(&v->u.waves.rumble_b, as_lp1(&v->u.waves.rumble_a, w));
    return as_mul_q15(as_mul_q15(low, env) * 2 + foam + rumble, G_WAVES);
}

// ---- 篝火 ----

static void fire_init(as_voice_t *v) {
    as_lp1_init(&v->u.fire.rumble_a, 380);
    as_lp1_init(&v->u.fire.rumble_b, 380);
    as_lp1_init(&v->u.fire.hiss_hp, 3200);
    as_lp1_init(&v->u.fire.pop_lp, 1100);
    as_drift_init(&v->u.fire.flicker, 15000, 32767, 40, 24000);
}

static int32_t fire_sample(as_voice_t *v) {
    as_rng_t *r = &v->rng;
    if ((v->t & SLOW_MASK) == 0) as_drift(&v->u.fire.flicker, r);
    const int32_t flicker = v->u.fire.flicker.value;
    const int32_t w = as_rng_s16(r);

    const int32_t rumble = as_lp1(&v->u.fire.rumble_b, as_lp1(&v->u.fire.rumble_a, w)) * 3;
    const int32_t hiss = as_hp1(&v->u.fire.hiss_hp, w) / 8;

    // 噼啪：平时每秒 3 次左右；偶尔进入 40–220 ms 的"爆裂簇"，密度骤增。
    if (v->u.fire.burst) {
        v->u.fire.burst--;
    } else if (as_rng_chance(r, 7, 10 * AS_SAMPLE_RATE)) {
        v->u.fire.burst = MS(as_rng_range(r, 40, 220));
    }
    const uint32_t rate = v->u.fire.burst ? 70u : 3u;
    if (as_rng_chance(r, rate, AS_SAMPLE_RATE)) {
        as_partial_t *c = grab(v->u.fire.cracks, AS_FIRE_CRACKS);
        c->env = AS_Q30 - 1;
        if (as_rng_chance(r, 1, 5)) {
            c->inc = 1;   // 闷响的"啵"：经过低通
            c->k = as_decay_coef(as_rng_range(r, 5, 14));
            c->amp = rand_amp(r, 8000, 20000);
        } else {
            c->inc = 0;   // 清脆的"啪"
            c->k = as_decay_coef(as_rng_range(r, 1, 3));
            c->amp = rand_amp(r, 2000, 20000);
        }
    }
    int32_t cracks = 0, pops = 0;
    for (int i = 0; i < AS_FIRE_CRACKS; i++) {
        as_partial_t *c = &v->u.fire.cracks[i];
        if (partial_idle(c)) continue;
        const int32_t s = grain_noise(c, as_rng_s16(r));
        if (c->inc) pops += s;
        else cracks += s;
    }
    pops = as_lp1(&v->u.fire.pop_lp, pops) * 2;
    return as_mul_q15(as_mul_q15(rumble + hiss, flicker) + cracks + pops, G_FIRE);
}

// ---- 溪流 ----

static void stream_init(as_voice_t *v) {
    as_svf_init(&v->u.stream.flow, 900, 9);
    as_lp1_init(&v->u.stream.flow_lp, 2800);
    as_drift_init(&v->u.stream.swirl, 8000, 32767, 60, 20000);
}

static int32_t stream_sample(as_voice_t *v) {
    as_rng_t *r = &v->rng;
    if ((v->t & SLOW_MASK) == 0) {
        const int32_t s = as_drift(&v->u.stream.swirl, r);
        as_svf_set(&v->u.stream.flow, 650 + (int32_t)(900 * (int64_t)s / 32768), 9);
    }
    const int32_t swirl = v->u.stream.swirl.value;
    const int32_t w = as_rng_s16(r);
    const int32_t band = as_svf(&v->u.stream.flow, w, NULL, NULL);
    const int32_t flow = as_mul_q15(as_lp1(&v->u.stream.flow_lp, band), 9000 + swirl / 2);

    // 水泡：每秒约 30–60 个，频率 350–1500 Hz，越小的泡越高、越短，音高随衰减上滑。
    const uint32_t rate = 30u + (uint32_t)(30 * swirl / 32768);
    if (as_rng_chance(r, rate, AS_SAMPLE_RATE)) {
        as_partial_t *b = grab(v->u.stream.bubbles, AS_STREAM_BUBBLES);
        const int32_t hz = 350 + (int32_t)((int64_t)1150 * as_rng_range(r, 0, 32767) / 32768 *
                                           as_rng_range(r, 0, 32767) / 32768);
        const int32_t tau = 9000 / (hz / 10 + 10);   // 约 8–24 ms
        b->env = AS_Q30 - 1;
        b->k = as_decay_coef(tau);
        b->inc = as_phase_inc_x16(hz * 16);
        // 生命周期约 3τ 内音高上升 40–90%。
        const uint32_t life = MS(3 * tau);
        b->inc_slope = (int32_t)((uint64_t)b->inc * (uint32_t)as_rng_range(r, 40, 90) / 100u / life);
        b->amp = rand_amp(r, 3000, 16000);
    }
    int32_t bubbles = 0;
    for (int i = 0; i < AS_STREAM_BUBBLES; i++) bubbles += grain_sine(&v->u.stream.bubbles[i]);
    return as_mul_q15(flow + bubbles, G_STREAM);
}

// ---- 山风 ----

static void wind_init(as_voice_t *v) {
    as_svf_init(&v->u.wind.howl, 400, 30);
    as_lp1_init(&v->u.wind.rumble, 140);
    as_drift_init(&v->u.wind.pitch, 0, 32767, 8, 12000);
    as_drift_init(&v->u.wind.gust, 5000, 32767, 6, 16000);
}

static int32_t wind_sample(as_voice_t *v) {
    as_rng_t *r = &v->rng;
    if ((v->t & SLOW_MASK) == 0) {
        const int32_t p = as_drift(&v->u.wind.pitch, r);
        const int32_t g = as_drift(&v->u.wind.gust, r);
        // 阵风越强，呼啸越高。
        const int32_t hz = 230 + (int32_t)(420 * (int64_t)p / 32768) + (int32_t)(260 * (int64_t)g / 32768);
        as_svf_set(&v->u.wind.howl, hz, 30);
    }
    const int32_t gust = v->u.wind.gust.value;
    const int32_t w = as_rng_s16(r);
    int32_t low;
    const int32_t band = as_svf(&v->u.wind.howl, w, &low, NULL);
    const int32_t rumble = as_lp1(&v->u.wind.rumble, w) * 3;
    const int32_t body = band / 3 + low / 4 + rumble;
    return as_mul_q15(as_mul_q15(body, gust), G_WIND);
}

// ---- 虫鸣 ----

#define BUG_PULSE MS(18)
#define BUG_GAP MS(14)
#define BUG_RAMP MS(3)

static void bug_schedule(as_voice_t *v, int i, uint32_t min_ms, uint32_t max_ms) {
    v->u.crickets.bugs[i].next = v->t + MS(as_rng_range(&v->rng, (int32_t)min_ms, (int32_t)max_ms));
    v->u.crickets.bugs[i].pulses = 0;
}

static void crickets_init(as_voice_t *v) {
    static const int32_t HZ[AS_CRICKETS] = { 4300, 4700, 5100 };
    static const int32_t AMP[AS_CRICKETS] = { 32767, 21000, 13000 };   // 远近不同
    as_pink_init(&v->u.crickets.pink);
    as_lp1_init(&v->u.crickets.night_lp, 700);
    for (int i = 0; i < AS_CRICKETS; i++) {
        v->u.crickets.bugs[i].inc = as_phase_inc_x16((HZ[i] + as_rng_range(&v->rng, -120, 120)) * 16);
        v->u.crickets.bugs[i].phase = 0;
        v->u.crickets.bugs[i].amp = AMP[i];
        bug_schedule(v, i, 50, 900);
    }
}

static int32_t crickets_sample(as_voice_t *v) {
    int32_t sum = 0;
    for (int i = 0; i < AS_CRICKETS; i++) {
        as_bug_t *b = &v->u.crickets.bugs[i];
        if (b->pulses == 0) {
            if ((int32_t)(v->t - b->next) < 0) continue;
            b->pulses = (uint8_t)as_rng_range(&v->rng, 3, 4);
            b->pulse_pos = 0;
        }
        int32_t env = 0;
        const uint32_t p = b->pulse_pos;
        if (p < BUG_PULSE) {
            env = 32767;
            if (p < BUG_RAMP) env = (int32_t)(p * 32767u / BUG_RAMP);
            else if (p > BUG_PULSE - BUG_RAMP) env = (int32_t)((BUG_PULSE - p) * 32767u / BUG_RAMP);
            sum += as_mul_q15(as_mul_q15(as_sine(b->phase), b->amp), env);
        }
        b->phase += b->inc;
        if (++b->pulse_pos >= BUG_PULSE + BUG_GAP) {
            b->pulse_pos = 0;
            if (--b->pulses == 0) {
                // 偶尔歇一会儿，免得节奏过于机械。
                if (as_rng_chance(&v->rng, 1, 8)) bug_schedule(v, i, 1500, 4000);
                else bug_schedule(v, i, 350, 900);
            }
        }
    }
    const int32_t night = as_lp1(&v->u.crickets.night_lp, as_pink(&v->u.crickets.pink, as_rng_s16(&v->rng)));
    return as_mul_q15(sum / 2 + night / 8, G_CRICKETS);
}

// ---- 颂钵 ----

#define BOWL_ATTACK MS(12)

typedef struct {
    int32_t ratio_x1000;
    int32_t amp;
    int32_t tau_ms;
} bowl_mode_t;

// 钵的振动模态：约 1 : 2.76 : 5.4 : 8.9，每个模态成对略微失谐，产生"嗡——"的拍频。
static const bowl_mode_t BOWL_MODES[AS_BOWL_PARTIALS] = {
    { 1000, 9000, 7000 }, { 1004, 7500, 7000 },  { 2760, 5200, 4000 },
    { 2766, 4200, 4000 }, { 5400, 2600, 1800 }, { 8930, 1300, 900 },
};
// 五声音阶（Hz × 16）：C4 D4 E4 G4 A4。再低的基音小喇叭放不出来。
static const int32_t BOWL_NOTES_X16[] = { 262 * 16, 294 * 16, 330 * 16, 392 * 16, 440 * 16 };
#define BOWL_NOTE_COUNT ((int)(sizeof BOWL_NOTES_X16 / sizeof BOWL_NOTES_X16[0]))

static void bowl_strike(as_voice_t *v) {
    as_rng_t *r = &v->rng;
    uint8_t note = (uint8_t)as_rng_range(r, 0, BOWL_NOTE_COUNT - 1);
    if (note == v->u.bowl.note) note = (uint8_t)((note + 2) % BOWL_NOTE_COUNT);
    v->u.bowl.note = note;
    const int32_t force = as_rng_range(r, 24000, 32767);
    for (int i = 0; i < AS_BOWL_PARTIALS; i++) {
        as_partial_t *p = &v->u.bowl.partials[i];
        // 相位保持连续，只换频率与幅度，旧余韵不会被硬切出咔哒声。
        p->inc = as_phase_inc_x16((int32_t)((int64_t)BOWL_NOTES_X16[note] * BOWL_MODES[i].ratio_x1000 / 1000));
        p->inc_slope = 0;
        p->k = as_decay_coef(BOWL_MODES[i].tau_ms);
        p->amp = as_mul_q15(BOWL_MODES[i].amp, force);
    }
    v->u.bowl.next = v->t + MS(as_rng_range(r, 10000, 17000));
}

static void bowl_init(as_voice_t *v) {
    as_pink_init(&v->u.bowl.room);
    as_lp1_init(&v->u.bowl.room_lp, 500);
    v->u.bowl.note = 0xFF;
    v->u.bowl.next = MS(300);
}

static int32_t bowl_sample(as_voice_t *v) {
    if ((int32_t)(v->t - v->u.bowl.next) >= 0) {
        bowl_strike(v);
        v->u.bowl.attack = BOWL_ATTACK;
    }
    const uint16_t attack = v->u.bowl.attack;
    int32_t sum = 0;
    for (int i = 0; i < AS_BOWL_PARTIALS; i++) {
        as_partial_t *p = &v->u.bowl.partials[i];
        if (p->inc == 0) continue;
        // 敲击后 12 ms 内包络从当前值平滑升到满：木槌的软起音，也不会切断上一击的余韵。
        if (attack) p->env += (AS_Q30 - 1 - p->env) / attack;
        sum += as_mul_q15(as_mul_q15(as_sine(p->phase), p->amp), p->env >> 15);
        p->phase += p->inc;
        p->env = as_decay(p->env, p->k);
    }
    if (attack) v->u.bowl.attack--;
    const int32_t room = as_lp1(&v->u.bowl.room_lp, as_pink(&v->u.bowl.room, as_rng_s16(&v->rng))) / 6;
    return as_mul_q15(sum + room, G_BOWL);
}

// ---- 白 / 粉 / 棕噪音 ----

static void noise_init(as_voice_t *v) {
    as_pink_init(&v->u.noise.pink);
    as_lp1_init(&v->u.noise.lp, 150);   // 棕噪音：150 Hz 以上 −6 dB/倍频程
    as_lp1_init(&v->u.noise.dc, 60);
}

static int32_t noise_sample(as_voice_t *v) {
    const int32_t w = as_rng_s16(&v->rng);
    switch (v->id) {
    case AS_SOUND_WHITE: return as_mul_q15(w, G_WHITE);
    case AS_SOUND_PINK: return as_mul_q15(as_pink(&v->u.noise.pink, w), G_PINK);
    default: {
        const int32_t brown = as_lp1(&v->u.noise.lp, w) * 5;
        return as_mul_q15(as_hp1(&v->u.noise.dc, brown), G_BROWN);
    }
    }
}

// ---- 接口 ----

void as_voice_init(as_voice_t *v, uint8_t id, uint32_t seed) {
    memset(v, 0, sizeof *v);
    as_sine_init();
    v->id = id < AS_SOUND_COUNT ? id : AS_SOUND_WHITE;
    as_rng_seed(&v->rng, seed * 2654435761u + 12345u);
    switch (v->id) {
    case AS_SOUND_RAIN: rain_init(v); break;
    case AS_SOUND_WAVES: waves_init(v); break;
    case AS_SOUND_FIRE: fire_init(v); break;
    case AS_SOUND_STREAM: stream_init(v); break;
    case AS_SOUND_WIND: wind_init(v); break;
    case AS_SOUND_CRICKETS: crickets_init(v); break;
    case AS_SOUND_BOWL: bowl_init(v); break;
    default: noise_init(v); break;
    }
}

void as_voice_render(as_voice_t *v, int16_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int32_t s;
        switch (v->id) {
        case AS_SOUND_RAIN: s = rain_sample(v); break;
        case AS_SOUND_WAVES: s = waves_sample(v); break;
        case AS_SOUND_FIRE: s = fire_sample(v); break;
        case AS_SOUND_STREAM: s = stream_sample(v); break;
        case AS_SOUND_WIND: s = wind_sample(v); break;
        case AS_SOUND_CRICKETS: s = crickets_sample(v); break;
        case AS_SOUND_BOWL: s = bowl_sample(v); break;
        default: s = noise_sample(v); break;
        }
        out[i] = (int16_t)as_clamp16(s);
        v->t++;
    }
}
