// tests/test_mn_sound.c —— click 合成主机测试：首尾归零、不削波、峰值与力度、长度上限、确定性。
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mn_sound.h"
#include "mn_test.h"

static int peak_of(const int16_t *x) {
    int p = 0;
    for (uint32_t i = 0; i < MN_CLICK_LEN; i++) {
        const int a = x[i] < 0 ? -x[i] : x[i];
        if (a > p) p = a;
    }
    return p;
}

static long long energy_of(const int16_t *x) {
    long long e = 0;
    for (uint32_t i = 0; i < MN_CLICK_LEN; i++) e += (long long)x[i] * x[i];
    return e;
}

int main(void) {
    static const int peaks[MN_TICK_KIND_COUNT] = { MN_PEAK_ACCENT, MN_PEAK_BEAT, MN_PEAK_SUB, MN_PEAK_COUNT };
    // click 必须短于最快 tick 间隔：250 BPM × 4 细分 = 每 tick 960 样本。
    CHECK(MN_CLICK_LEN < (uint32_t)MN_SAMPLE_RATE * 60u / (MN_BPM_MAX * MN_SUBDIV_MAX));
    for (uint8_t s = 0; s < MN_SOUND_COUNT; s++) {
        int16_t clicks[MN_TICK_KIND_COUNT][MN_CLICK_LEN];
        for (uint8_t k = 0; k < MN_TICK_KIND_COUNT; k++) {
            int16_t *x = clicks[k];
            mn_sound_render(s, k, x);
            CHECK(x[0] == 0);                         // 起音从 0 开始，无直流跳变
            CHECK(x[MN_CLICK_LEN - 1] == 0);          // 收尾回到 0
            CHECK_NEAR(peak_of(x), peaks[k], 2);      // 归一化到目标峰值，不削波
            for (uint32_t i = 1; i < 24; i++) {       // 起音斜坡内幅度受限
                const int a = x[i] < 0 ? -x[i] : x[i];
                CHECK(a <= (int)((long long)peaks[k] * 2 * i / 24 + 2000));
            }
            int16_t again[MN_CLICK_LEN];
            mn_sound_render(s, k, again);
            CHECK(memcmp(x, again, sizeof again) == 0);   // 确定性
        }
        // 力度区分：重音能量 > 普通拍 > 细分音。
        CHECK(energy_of(clicks[MN_TICK_ACCENT]) > energy_of(clicks[MN_TICK_BEAT]));
        CHECK(energy_of(clicks[MN_TICK_BEAT]) > energy_of(clicks[MN_TICK_SUB]));
    }
    // 倒数提示音与音色无关，且与任何节拍音都不同。
    int16_t c0[MN_CLICK_LEN], c1[MN_CLICK_LEN];
    mn_sound_render(MN_SOUND_BEEP, MN_TICK_COUNT, c0);
    mn_sound_render(MN_SOUND_COWBELL, MN_TICK_COUNT, c1);
    CHECK(memcmp(c0, c1, sizeof c0) == 0);
    for (uint8_t s = 0; s < MN_SOUND_COUNT; s++) {
        for (uint8_t k = 0; k < MN_TICK_COUNT; k++) {
            int16_t x[MN_CLICK_LEN];
            mn_sound_render(s, k, x);
            CHECK(memcmp(x, c0, sizeof x) != 0);
        }
    }
    // 不同音色的波形不同；越界参数按 0 处理。
    int16_t a[MN_CLICK_LEN], b[MN_CLICK_LEN];
    mn_sound_render(MN_SOUND_BEEP, MN_TICK_BEAT, a);
    mn_sound_render(MN_SOUND_WOOD, MN_TICK_BEAT, b);
    CHECK(memcmp(a, b, sizeof a) != 0);
    mn_sound_render(99, 99, a);
    mn_sound_render(0, 0, b);
    CHECK(memcmp(a, b, sizeof a) == 0);
    puts("Metronome sound synthesis tests: PASS");
    return 0;
}
