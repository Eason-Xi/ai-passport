// tests/test_as_mixer.c —— 三层混音器的主机测试：淡入淡出、交叉淡化、限幅、渐弱。
#include <stdlib.h>
#include <string.h>

#include "as_mixer.h"
#include "as_test.h"

#define BLOCK 240
static as_mixer_t mx;
static int16_t buf[BLOCK];

static int32_t peak_of(void) {
    int32_t p = 0;
    for (int i = 0; i < BLOCK; i++) p = abs(buf[i]) > p ? abs(buf[i]) : p;
    return p;
}

static void run_ms(int ms) {
    for (int i = 0; i < ms / 15; i++) as_mixer_render(&mx, buf, BLOCK);
}

static void test_level_table_and_limiter(void) {
    CHECK(as_level_gain(0) == 0);
    CHECK(as_level_gain(AS_LEVEL_MAX) == 32767);
    CHECK(as_level_gain(200) == 32767);
    for (uint8_t l = 1; l <= AS_LEVEL_MAX; l++) CHECK(as_level_gain(l) > as_level_gain(l - 1));
    for (int32_t x = -20000; x <= 20000; x += 997) CHECK(as_soft_limit(x) == x);
    int32_t prev = as_soft_limit(20000);
    for (int32_t x = 20001; x < 400000; x += 13) {
        const int32_t y = as_soft_limit(x);
        CHECK(y >= prev && y < 32767);
        CHECK(as_soft_limit(-x) == -y);
        prev = y;
    }
}

static void test_silent_until_play(void) {
    as_mixer_init(&mx);
    as_mixer_set_layer(&mx, 0, AS_SOUND_PINK, 10);
    CHECK(as_mixer_silent(&mx));
    as_mixer_render(&mx, buf, BLOCK);
    CHECK(peak_of() == 0);
    // 开始播放：约 1 s 淡入，第一块很轻，之后逐渐变响。
    as_mixer_set_playing(&mx, true);
    CHECK(!as_mixer_silent(&mx));
    as_mixer_render(&mx, buf, BLOCK);
    CHECK(peak_of() < 2000);
    run_ms(900);
    CHECK(mx.transport < 32768);
    run_ms(200);
    CHECK(mx.transport == 32768);
    CHECK(mx.layers[0].gain == as_level_gain(10));
    CHECK(mx.layers[0].meter > 100);
    CHECK(mx.meter > 100);
    // 暂停：约 1 s 淡出后报告静音，输出全零。
    as_mixer_set_playing(&mx, false);
    run_ms(600);
    CHECK(!as_mixer_silent(&mx));
    run_ms(500);
    CHECK(as_mixer_silent(&mx));
    as_mixer_render(&mx, buf, BLOCK);
    CHECK(peak_of() == 0);
}

// 增益逐样本插值：用直流不变的"声部"不好构造，改为检查一块内的增益斜率上限。
static void test_ramps_are_bounded(void) {
    as_mixer_init(&mx);
    as_mixer_set_layer(&mx, 0, AS_SOUND_BROWN, 10);
    as_mixer_set_playing(&mx, true);
    int32_t prev = mx.transport;
    for (int i = 0; i < 100; i++) {
        as_mixer_render(&mx, buf, BLOCK);
        CHECK(mx.transport - prev <= 2 * BLOCK);
        prev = mx.transport;
    }
    // 层音量从 10 调到 1：约 0.3 s 平滑下降。
    as_mixer_set_layer(&mx, 0, AS_SOUND_BROWN, 1);
    int32_t g = mx.layers[0].gain;
    as_mixer_render(&mx, buf, BLOCK);
    CHECK(g - mx.layers[0].gain <= 7 * BLOCK);
    run_ms(300);
    CHECK(mx.layers[0].gain == as_level_gain(1));
}

static void test_crossfade_and_queue(void) {
    as_mixer_init(&mx);
    as_mixer_set_layer(&mx, 1, AS_SOUND_RAIN, 8);
    as_mixer_set_playing(&mx, true);
    run_ms(1200);
    // 换声：旧声部进入淡出槽，新声部从 0 开始淡入。
    as_mixer_set_layer(&mx, 1, AS_SOUND_FIRE, 8);
    CHECK(mx.layers[1].old_active);
    CHECK(mx.layers[1].old.id == AS_SOUND_RAIN);
    CHECK(mx.layers[1].cur.id == AS_SOUND_FIRE);
    CHECK(mx.layers[1].gain == 0);
    // 淡化中再连续换两次：只排队最新的一个，不硬切。
    as_mixer_set_layer(&mx, 1, AS_SOUND_WIND, 8);
    as_mixer_set_layer(&mx, 1, AS_SOUND_STREAM, 8);
    CHECK(mx.layers[1].pending && mx.layers[1].pending_sound == AS_SOUND_STREAM);
    CHECK(mx.layers[1].sound == AS_SOUND_FIRE);
    run_ms(330);
    // 第一轮淡化结束，排队的声音开始接替（火 → 溪流）。
    CHECK(!mx.layers[1].pending);
    CHECK(mx.layers[1].sound == AS_SOUND_STREAM);
    run_ms(700);
    CHECK(!mx.layers[1].old_active);
    CHECK(mx.layers[1].gain == as_level_gain(8));
    // 改回原声音的排队请求等于取消排队。
    as_mixer_set_layer(&mx, 1, AS_SOUND_BOWL, 8);
    as_mixer_set_layer(&mx, 1, AS_SOUND_PINK, 8);
    as_mixer_set_layer(&mx, 1, AS_SOUND_BOWL, 8);
    CHECK(!mx.layers[1].pending);
    // 设为空：当前声部淡出，层变为空。
    run_ms(600);
    as_mixer_set_layer(&mx, 1, AS_SOUND_NONE, 8);
    CHECK(mx.layers[1].sound == AS_SOUND_NONE && mx.layers[1].old_active);
    run_ms(400);
    CHECK(!mx.layers[1].old_active);
    CHECK(mx.layers[1].meter == 0);
    // 越界编号视为空；越界层号忽略。
    as_mixer_set_layer(&mx, 2, 77, 5);
    CHECK(mx.layers[2].sound == AS_SOUND_NONE);
    as_mixer_set_layer(&mx, 3, AS_SOUND_RAIN, 5);
    as_mixer_set_layer(&mx, -1, AS_SOUND_RAIN, 5);
}

// 三层满音量叠加：限幅后不会越界，平均电平在合理范围内。
static void test_three_layers_full(void) {
    as_mixer_init(&mx);
    as_mixer_set_layer(&mx, 0, AS_SOUND_BROWN, 10);
    as_mixer_set_layer(&mx, 1, AS_SOUND_WAVES, 10);
    as_mixer_set_layer(&mx, 2, AS_SOUND_BOWL, 10);
    as_mixer_set_playing(&mx, true);
    int32_t peak = 0;
    for (int i = 0; i < 60 * 1000 / 15; i++) {
        as_mixer_render(&mx, buf, BLOCK);
        const int32_t p = peak_of();
        peak = p > peak ? p : peak;
    }
    CHECK(peak <= 32767);
    CHECK(peak > 20000);
}

// 睡眠定时渐弱：渐弱增益为 0 时输出归零；再次播放前恢复。
static void test_fade(void) {
    as_mixer_init(&mx);
    as_mixer_set_layer(&mx, 0, AS_SOUND_WHITE, 10);
    as_mixer_set_playing(&mx, true);
    run_ms(1200);
    CHECK(peak_of() > 3000);
    as_mixer_set_fade(&mx, 16384);
    run_ms(300);
    CHECK(mx.fade == 16384);
    as_mixer_set_fade(&mx, -5);
    run_ms(600);
    CHECK(mx.fade == 0);
    CHECK(peak_of() == 0);
    as_mixer_set_fade(&mx, 99999);
    CHECK(mx.fade_target == 32768);
}

int main(void) {
    test_level_table_and_limiter();
    test_silent_until_play();
    test_ramps_are_bounded();
    test_crossfade_and_queue();
    test_three_layers_full();
    test_fade();
    printf("test_as_mixer: PASS\n");
    return 0;
}
