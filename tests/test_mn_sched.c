// tests/test_mn_sched.c —— 节拍调度主机测试：样本级位置、零漂移、参数变更边界、分块无关。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mn_sched.h"

// 以任意块长推进 total 个样本，收集全部 tick 的绝对位置。
static size_t collect(mn_sched_t *s, uint64_t total, uint32_t block, uint64_t *pos, mn_tick_t *info,
                      size_t cap) {
    size_t n = 0;
    uint64_t done = 0;
    mn_tick_t ticks[64];
    while (done < total) {
        const uint32_t len = (uint32_t)(total - done < block ? total - done : block);
        const size_t got = mn_sched_advance(s, len, ticks, 64);
        for (size_t i = 0; i < got && n < cap; i++, n++) {
            pos[n] = done + ticks[i].offset;
            if (info) info[n] = ticks[i];
        }
        done += len;
    }
    return n;
}

static uint64_t s_pos[200000];
static mn_tick_t s_info[2000];

// 1 小时零漂移：每个 tick 都精确落在 floor(k*fs*60/D)。
static void test_no_drift(void) {
    const uint16_t bpms[] = { 30, 61, 97, 120, 133, 177, 250 };
    for (size_t b = 0; b < sizeof bpms / sizeof bpms[0]; b++) {
        for (uint8_t sub = 1; sub <= MN_SUBDIV_MAX; sub++) {
            mn_sched_t s;
            mn_meter_t m = { .bpm = bpms[b], .beats = 4, .subdiv = sub, .accent = true };
            mn_sched_start(&s, &m);
            const uint64_t hour = (uint64_t)MN_SAMPLE_RATE * 3600u;
            const size_t n = collect(&s, hour, 240, s_pos, NULL, 200000);
            assert(n == (size_t)((uint64_t)bpms[b] * sub * 60u));
            for (size_t k = 0; k < n; k++) assert(s_pos[k] == mn_sched_ideal_pos(k, bpms[b], sub));
        }
    }
}

// 拍序号、细分序号与重音类型。
static void test_pattern(void) {
    mn_sched_t s;
    mn_meter_t m = { .bpm = 120, .beats = 3, .subdiv = 2, .accent = true };
    mn_sched_start(&s, &m);
    const size_t n = collect(&s, 16000u * 4u, 256, s_pos, s_info, 2000);
    assert(n == 16);  // 120 BPM × 2 细分 × 4 秒
    for (size_t i = 0; i < n; i++) {
        assert(s_info[i].sub == i % 2);
        assert(s_info[i].beat == (i / 2) % 3);
        assert(s_info[i].beat_no == i / 2);
        const uint8_t want = s_info[i].sub ? MN_TICK_SUB : (s_info[i].beat == 0 ? MN_TICK_ACCENT : MN_TICK_BEAT);
        assert(s_info[i].kind == want);
    }
    // 拍数为 1 或关闭重音时没有 ACCENT。
    m.beats = 1;
    mn_sched_start(&s, &m);
    collect(&s, 16000u * 2u, 256, s_pos, s_info, 2000);
    for (size_t i = 0; i < 8; i++) assert(s_info[i].kind != MN_TICK_ACCENT);
}

// 分块大小不影响结果。
static void test_block_independent(void) {
    static uint64_t a[4000], b[4000];
    mn_meter_t m = { .bpm = 137, .beats = 7, .subdiv = 3, .accent = true };
    mn_sched_t s1, s2;
    mn_sched_start(&s1, &m);
    mn_sched_start(&s2, &m);
    const size_t n1 = collect(&s1, 16000u * 60u, 256, a, NULL, 4000);
    const size_t n2 = collect(&s2, 16000u * 60u, 97, b, NULL, 4000);
    assert(n1 == n2 && memcmp(a, b, n1 * sizeof a[0]) == 0);
}

// BPM 立即按新间隔重排；细分在下一拍头生效；拍数缩小时回绕。
static void test_changes(void) {
    mn_sched_t s;
    mn_tick_t t[16];
    mn_meter_t m = { .bpm = 60, .beats = 4, .subdiv = 1, .accent = true };
    mn_sched_start(&s, &m);
    assert(mn_sched_advance(&s, 1000, t, 16) == 1 && t[0].offset == 0);
    m.bpm = 120;  // 下一拍应在 8000 而不是 16000
    mn_sched_set(&s, &m);
    assert(mn_sched_advance(&s, 7001, t, 16) == 1 && t[0].offset == 7000);
    // 已过期：上一 tick 在 8000，当前位置 8000+... 改成 250 BPM（间隔 3840）且已越过时在块头补响。
    assert(mn_sched_advance(&s, 5000, t, 16) == 0);  // pos=13000，下一个在 16000
    m.bpm = 250;  // last=8000, 8000+3840=11840 < 13000 → 13000 补响
    mn_sched_set(&s, &m);
    assert(mn_sched_advance(&s, 100, t, 16) == 1 && t[0].offset == 0);

    // 细分在拍头生效：当前拍内仍是 1 个 tick。
    m = (mn_meter_t){ .bpm = 60, .beats = 4, .subdiv = 1, .accent = true };
    mn_sched_start(&s, &m);
    assert(mn_sched_advance(&s, 8000, t, 16) == 1);
    m.subdiv = 4;
    mn_sched_set(&s, &m);
    assert(mn_sched_advance(&s, 8000, t, 16) == 0);           // 本拍不插入细分
    assert(mn_sched_advance(&s, 16000, t, 16) == 4);          // 下一拍起四等分
    assert(t[0].offset == 0 && t[1].offset == 4000 && t[2].offset == 8000 && t[3].offset == 12000);
    assert(t[0].sub == 0 && t[3].sub == 3 && t[3].kind == MN_TICK_SUB);

    // 拍数从 4 缩到 2：处于第 3 拍时，下一拍回到第 1 拍。
    m = (mn_meter_t){ .bpm = 60, .beats = 4, .subdiv = 1, .accent = true };
    mn_sched_start(&s, &m);
    assert(mn_sched_advance(&s, 16000u * 3u, t, 16) == 3 && t[2].beat == 2);
    m.beats = 2;
    mn_sched_set(&s, &m);
    assert(mn_sched_advance(&s, 16000, t, 16) == 1 && t[0].beat == 0 && t[0].kind == MN_TICK_ACCENT);
}

// 混音：click 按 tick 位置写入、跨块连续、静音时全 0。
static void test_render(void) {
    static int16_t accent[50], beat[50], sub[50];
    for (int i = 0; i < 50; i++) { accent[i] = (int16_t)(1000 + i); beat[i] = (int16_t)(2000 + i); sub[i] = 3; }
    const mn_bank_t bank = { .pcm = { accent, beat, sub }, .len = { 50, 50, 50 } };
    mn_sched_t s;
    mn_voice_t v;
    mn_tick_t t[16];
    size_t n;
    int16_t out[30];
    mn_meter_t m = { .bpm = 120, .beats = 4, .subdiv = 1, .accent = true };
    mn_sched_start(&s, &m);
    mn_voice_reset(&v);
    mn_render(&s, &v, &bank, false, out, 30, t, 16, &n);
    assert(n == 1 && out[0] == 1000 && out[29] == 1029);
    mn_render(&s, &v, &bank, false, out, 30, t, 16, &n);
    assert(n == 0 && out[0] == 1030 && out[19] == 1049 && out[20] == 0);
    assert(!mn_voice_active(&v));
    mn_render(&s, &v, &bank, true, out, 30, t, 16, &n);
    for (int i = 0; i < 30; i++) assert(out[i] == 0);
    // 停止后只渲染尾巴：sched 为 NULL 不产生新 tick。
    mn_sched_start(&s, &m);
    mn_render(&s, &v, &bank, false, out, 10, t, 16, &n);
    mn_render(NULL, &v, &bank, false, out, 30, t, 16, &n);
    assert(n == 0 && out[0] == 1010 && out[29] == 1039);
}

int main(void) {
    mn_meter_t bad = mn_meter_sanitize((mn_meter_t){ .bpm = 5, .beats = 0, .subdiv = 9 });
    assert(bad.bpm == MN_BPM_MIN && bad.beats == 1 && bad.subdiv == MN_SUBDIV_MAX);
    test_no_drift();
    test_pattern();
    test_block_independent();
    test_changes();
    test_render();
    puts("Metronome scheduler tests: PASS");
    return 0;
}
