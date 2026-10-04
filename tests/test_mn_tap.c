// tests/test_mn_tap.c —— 敲击测速主机测试：稳定敲击、按键抖动、超时重置、换速、夹紧、抖动容差。
#include <stdint.h>
#include <stdio.h>

#include "mn_tap.h"
#include "mn_test.h"

static void tap_series(mn_tap_t *t, int64_t *now, int count, int64_t interval_us) {
    for (int i = 0; i < count; i++) {
        CHECK(mn_tap_add(t, *now));
        *now += interval_us;
    }
}

int main(void) {
    mn_tap_t t;
    int64_t now = 1000000;

    // 不足一个间隔时没有结果。
    mn_tap_reset(&t);
    CHECK(mn_tap_bpm(&t) == 0 && mn_tap_count(&t) == 0);
    CHECK(mn_tap_add(&t, now));
    CHECK(mn_tap_bpm(&t) == 0 && mn_tap_count(&t) == 1);

    // 稳定 120 BPM（500 ms）。
    mn_tap_reset(&t);
    tap_series(&t, &now, 5, 500000);
    CHECK(mn_tap_bpm(&t) == 120 && mn_tap_count(&t) == 5);

    // 各速度的精确换算。
    const uint16_t bpms[] = { 30, 45, 60, 96, 133, 200, 250 };
    for (size_t i = 0; i < sizeof bpms / sizeof bpms[0]; i++) {
        mn_tap_reset(&t);
        now += 10000000;
        tap_series(&t, &now, 6, 60000000 / bpms[i]);
        CHECK_NEAR(mn_tap_bpm(&t), bpms[i], 1);
    }

    // ±5 ms 的按键轮询抖动：平均后误差不超过 1 BPM。
    mn_tap_reset(&t);
    now += 10000000;
    static const int jitter[] = { 0, 5000, -5000, 3000, -2000, 4000, -4000, 1000, -3000 };
    int64_t base = now;
    for (int i = 0; i < 9; i++) CHECK(mn_tap_add(&t, base + i * 600000LL + jitter[i]));
    CHECK_NEAR(mn_tap_bpm(&t), 100, 1);

    // 按键抖动（< 200 ms）被忽略，不计次数。
    mn_tap_reset(&t);
    now += 10000000;
    CHECK(mn_tap_add(&t, now));
    CHECK(!mn_tap_add(&t, now + 50000));
    CHECK(mn_tap_add(&t, now + 500000));
    CHECK(mn_tap_count(&t) == 2 && mn_tap_bpm(&t) == 120);

    // 停顿超过 2.5 s：重新开始。
    now += 500000 + 3000000;
    CHECK(mn_tap_add(&t, now));
    CHECK(mn_tap_count(&t) == 1 && mn_tap_bpm(&t) == 0);

    // 换速：在 120 BPM 序列后改敲 60 BPM，偏离 > 35% 时以上一下为起点重新累计。
    mn_tap_reset(&t);
    now += 10000000;
    tap_series(&t, &now, 4, 500000);      // 最后一次敲击在 now - 500000
    now += 500000;                        // 与上一下相隔 1 s
    CHECK(mn_tap_add(&t, now));
    CHECK(mn_tap_count(&t) == 2 && mn_tap_bpm(&t) == 60);

    // 只保留最近 8 个间隔：先敲很多 100 BPM，再逐渐过渡到 104 BPM（每步偏离很小，不触发重置）。
    mn_tap_reset(&t);
    now += 10000000;
    tap_series(&t, &now, 12, 600000);
    tap_series(&t, &now, 10, 576923);     // 104 BPM
    CHECK_NEAR(mn_tap_bpm(&t), 104, 1);

    // 结果夹紧到 30–250（2.4 s 间隔 = 25 BPM 被夹到 30）。
    mn_tap_reset(&t);
    now += 10000000;
    CHECK(mn_tap_add(&t, now));
    CHECK(mn_tap_add(&t, now + 2400000));
    CHECK(mn_tap_bpm(&t) == 30);
    puts("Metronome tap tempo tests: PASS");
    return 0;
}
