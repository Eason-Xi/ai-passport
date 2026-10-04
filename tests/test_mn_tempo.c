// tests/test_mn_tempo.c —— 速度术语、BPM 步进与长按节奏、节拍点排布与摆杆角度。
#include <stdint.h>
#include <stdio.h>

#include "mn_layout.h"
#include "mn_sched.h"
#include "mn_tempo.h"
#include "mn_test.h"

static void test_terms(void) {
    CHECK(mn_tempo_term(30) == MN_TEMPO_GRAVE);
    CHECK(mn_tempo_term(39) == MN_TEMPO_GRAVE);
    CHECK(mn_tempo_term(40) == MN_TEMPO_LARGO);
    CHECK(mn_tempo_term(65) == MN_TEMPO_LARGHETTO);
    CHECK(mn_tempo_term(66) == MN_TEMPO_ADAGIO);
    CHECK(mn_tempo_term(100) == MN_TEMPO_ANDANTE);
    CHECK(mn_tempo_term(108) == MN_TEMPO_MODERATO);
    CHECK(mn_tempo_term(120) == MN_TEMPO_ALLEGRO);
    CHECK(mn_tempo_term(160) == MN_TEMPO_VIVACE);
    CHECK(mn_tempo_term(199) == MN_TEMPO_PRESTO);
    CHECK(mn_tempo_term(250) == MN_TEMPO_PRESTISSIMO);
    // 术语随 BPM 单调不减。
    for (uint16_t b = MN_BPM_MIN; b < MN_BPM_MAX; b++) CHECK(mn_tempo_term(b) <= mn_tempo_term(b + 1));
}

static void test_bump(void) {
    CHECK(mn_bpm_clamp(10) == 30 && mn_bpm_clamp(999) == 250 && mn_bpm_clamp(77) == 77);
    CHECK(mn_bpm_bump(100, 1, 1) == 101 && mn_bpm_bump(100, -1, 1) == 99);
    CHECK(mn_bpm_bump(250, 1, 1) == 250 && mn_bpm_bump(30, -1, 1) == 30);
    CHECK(mn_bpm_bump(103, 1, 5) == 105 && mn_bpm_bump(103, -1, 5) == 100);
    CHECK(mn_bpm_bump(105, 1, 5) == 110 && mn_bpm_bump(105, -1, 5) == 100);
    CHECK(mn_bpm_bump(248, 1, 5) == 250 && mn_bpm_bump(32, -1, 5) == 30);
    // 长按节奏：先慢后快，最后 5 的倍数大步。
    CHECK(mn_repeat_delay_ms(0) == 120 && mn_repeat_step(0) == 1);
    CHECK(mn_repeat_delay_ms(8) == 50 && mn_repeat_step(23) == 1);
    CHECK(mn_repeat_step(24) == 5 && mn_repeat_delay_ms(24) == 90);
    // 从 30 长按到 250 不超过 6 秒。
    uint16_t bpm = 30;
    uint32_t elapsed = 0, n = 0;
    while (bpm < 250) {
        bpm = mn_bpm_bump(bpm, 1, mn_repeat_step(n));
        elapsed += mn_repeat_delay_ms(n);
        n++;
    }
    CHECK(elapsed < 6000);
}

static void test_dots(void) {
    mn_dot_t dots[12];
    for (uint8_t n = 1; n <= 12; n++) {
        CHECK(mn_layout_dots(n, dots) == n);
        for (uint8_t i = 0; i < n; i++) {
            CHECK(dots[i].d >= 8 && dots[i].d <= MN_DOT_MAX);
            CHECK(dots[i].x - dots[i].d / 2 >= MN_DOTS_X0);
            CHECK(dots[i].x + (dots[i].d + 1) / 2 <= MN_DOTS_X1);
            if (i) CHECK(dots[i].x - dots[i - 1].x >= dots[i].d + MN_DOT_GAP_MIN);
        }
        // 居中：左右留白差不超过 2 px。
        const int left = dots[0].x - dots[0].d / 2 - MN_DOTS_X0;
        const int right = MN_DOTS_X1 - (dots[n - 1].x + (dots[n - 1].d + 1) / 2);
        CHECK_NEAR(left, right, 2);
    }
    CHECK(mn_layout_dots(0, dots) == 1 && mn_layout_dots(20, dots) == 12);
}

static void test_pendulum(void) {
    CHECK(mn_pendulum_angle(0, 0, 300) == -300);   // 偶数拍头在左
    CHECK(mn_pendulum_angle(1, 0, 300) == 300);    // 奇数拍头在右
    CHECK_NEAR(mn_pendulum_angle(0, 32768, 300), 0, 1);   // 拍中竖直
    CHECK(mn_pendulum_angle(0, 65535, 300) > 299);        // 拍尾接近右极点，与下一拍头衔接
    // 单调：偶数拍内由左向右。
    int16_t prev = -301;
    for (uint32_t f = 0; f < 65536; f += 1024) {
        const int16_t a = mn_pendulum_angle(2, f, 300);
        CHECK(a >= prev);
        prev = a;
    }
}

int main(void) {
    test_terms();
    test_bump();
    test_dots();
    test_pendulum();
    puts("Metronome tempo/layout tests: PASS");
    return 0;
}
