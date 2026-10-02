// tests/test_yz_bell.c —— 提示音合成：长度、峰值不超限、淡入淡出到 0、确有声音、可重复播放。
#include <stdlib.h>

#include "yz_bell.h"
#include "yz_test.h"

static int16_t s_pcm[YZ_BELL_SAMPLES + 64];

static size_t render_all(yz_bell_t *bell) {
    size_t total = 0, n;
    while ((n = yz_bell_render(bell, s_pcm + total, 100)) > 0) total += n;
    return total;
}

int main(void) {
    yz_bell_t bell;
    yz_bell_start(&bell, 12000);
    CHECK(render_all(&bell) == YZ_BELL_SAMPLES);
    CHECK(yz_bell_render(&bell, s_pcm, 10) == 0);

    int peak = 0;
    long long energy_head = 0, energy_tail = 0;
    for (int i = 0; i < YZ_BELL_SAMPLES; i++) {
        const int v = abs(s_pcm[i]);
        if (v > peak) peak = v;
        if (i < YZ_BELL_RATE / 4) energy_head += (long long)v * v;
        if (i >= YZ_BELL_SAMPLES - YZ_BELL_RATE / 4) energy_tail += (long long)v * v;
    }
    CHECK(peak <= 12000 && peak > 6000);                // 不超过设定峰值，也不是近乎无声
    CHECK(abs(s_pcm[0]) < 200);                         // 淡入：起点安静，避免“咔嗒”
    CHECK(s_pcm[YZ_BELL_SAMPLES - 1] == 0);             // 淡出：终点归零
    CHECK(energy_tail * 20 < energy_head);              // 自然衰减

    // 重新开始得到相同波形（状态完整复位）。
    const int16_t first = s_pcm[1000];
    yz_bell_start(&bell, 12000);
    render_all(&bell);
    CHECK(s_pcm[1000] == first);
    printf("test_yz_bell: PASS\n");
    return 0;
}
