// tools/metronome_preview/preview_main.c —— 在主机上用真实 LVGL 渲染节拍器的各个页面。
// 由 tools/render_metronome_preview.py 编译运行：链接固件里的界面与状态机代码，按真实按键
// 流程驱动 mn_model_t，喂入模拟的节拍事件，逐页输出 PPM，并报告 LVGL 内存池占用与字形自检结果。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "mn_fonts.h"
#include "mn_layout.h"
#include "mn_model.h"
#include "mn_ui.h"

static uint16_t s_frame[MN_SCREEN_W * MN_SCREEN_H];
static uint8_t s_render_buf[MN_SCREEN_W * MN_SCREEN_H * 2];
static uint32_t s_tick_ms = 1000;
static const char *s_out_dir = ".";
static size_t s_peak;
static mn_model_t g;

static uint32_t tick_cb(void) {
    return s_tick_ms;
}

static int64_t now_us(void) {
    return (int64_t)s_tick_ms * 1000;
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    const uint16_t *src = (const uint16_t *)px_map;
    const int w = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&s_frame[y * MN_SCREEN_W + area->x1], src, (size_t)w * 2);
        src += w;
    }
    lv_display_flush_ready(display);
}

// 推进 ms 毫秒：每 20 ms 跑一次动画与 LVGL（与固件的 lv_timer 周期一致）。
static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
        s_tick_ms += 20;
        mn_ui_animate(now_us());
        lv_timer_handler();
    }
}

static void capture(const char *name) {
    run(40);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_out_dir, name);
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fprintf(file, "P6\n%d %d\n255\n", MN_SCREEN_W, MN_SCREEN_H);
    for (int i = 0; i < MN_SCREEN_W * MN_SCREEN_H; i++) {
        const uint16_t c = s_frame[i];
        const int r = (c >> 11) & 0x1F, gg = (c >> 5) & 0x3F, b = c & 0x1F;
        fputc((r << 3) | (r >> 2), file);
        fputc((gg << 2) | (gg >> 4), file);
        fputc((b << 3) | (b >> 2), file);
    }
    fclose(file);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const size_t used = mon.total_size - mon.free_size;
    if (mon.max_used > s_peak) s_peak = mon.max_used;
    printf("MEM %-26s used=%5zu peak=%5zu free=%5zu largest=%5zu\n", name, used, (size_t)mon.max_used,
           (size_t)mon.free_size, (size_t)mon.free_biggest_size);
}

static void apply(mn_fx_t fx) {
    if (fx & MN_FX_SCREEN) mn_ui_show(&g);
    else if (fx & (MN_FX_REFRESH | MN_FX_RUN)) mn_ui_update(&g);
    if (fx & MN_FX_TAP_FLASH) mn_ui_tap_flash(now_us());
}

static void key(mn_btn_t b, mn_ev_t e) {
    apply(mn_model_key(&g, b, e, s_tick_ms, now_us()));
}

static void tick(void) {
    apply(mn_model_tick(&g, s_tick_ms));
}

// 模拟音频任务送来的一个节拍事件，并推进到拍内 frac（0..1）处。
static void beat(uint8_t beat_i, uint8_t sub, uint32_t beat_no, float frac) {
    const mn_cfg_t *c = &g.cfg;
    const mn_beat_t b = {
        .play_us = now_us(), .beat_no = beat_no, .bpm = c->bpm, .beat = beat_i, .sub = sub,
        .subdiv = c->subdiv, .beats = c->beats,
        .kind = sub ? MN_TICK_SUB : (beat_i == 0 && c->accent && c->beats > 1 ? MN_TICK_ACCENT : MN_TICK_BEAT),
    };
    mn_ui_beat(&b, now_us());
    const uint32_t beat_ms = 60000u / c->bpm;
    run((uint32_t)(frac * (float)beat_ms));
}

// 模拟一个开始倒数音（剩余 remaining 秒），并推进 ms 毫秒。
static void count(uint8_t remaining, uint32_t ms) {
    const mn_beat_t b = { .play_us = now_us(), .bpm = g.cfg.bpm, .beat = remaining, .subdiv = g.cfg.subdiv,
                          .beats = g.cfg.beats, .kind = MN_TICK_COUNT };
    mn_ui_beat(&b, now_us());
    run(ms);
}

static void reset_model(void) {
    mn_cfg_t cfg;
    mn_cfg_default(&cfg);
    mn_model_init(&g, &cfg);
}

int main(int argc, char **argv) {
    if (argc > 1) s_out_dir = argv[1];
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *display = lv_display_create(MN_SCREEN_W, MN_SCREEN_H);
    lv_display_set_buffers(display, s_render_buf, NULL, sizeof s_render_buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_cb);

    // 字形自检：固件启动时同一段代码；再加一个反例，保证检查本身不会无条件通过。
    int missing = mn_fonts_selfcheck();
    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(&mn_zh18, &dsc, 0x9F98, 0) && !dsc.is_placeholder) {
        printf("FONT-CHECK-BROKEN: U+9F98 reported as covered\n");
        missing++;
    }
    printf("FONT missing=%d\n", missing);

    reset_model();
    mn_ui_init(true);
    mn_ui_set_battery(86);
    mn_ui_show(&g);
    capture("01_main_stopped");

    // 开始播放：先倒数 3 秒（摆锤摆到左侧待命），然后首拍重音，摆锤刚离开左极点。
    key(MN_BTN_OK, MN_EV_CLICK);
    run(100);
    count(3, 1000);
    count(2, 300);
    capture("01b_countdown");
    count(1, 1000);
    beat(0, 0, 0, 0.06f);
    capture("02_main_downbeat");
    beat(1, 0, 1, 0.5f);
    capture("03_main_beat2_mid");

    // 7/4、三连音、168 BPM：第 4 拍的第 3 个细分音。
    g.cfg.beats = 7;
    g.cfg.subdiv = 3;
    g.cfg.bpm = 168;
    mn_ui_update(&g);
    beat(3, 0, 10, 0.0f);
    beat(3, 2, 10, 0.25f);
    capture("04_main_7beats_triplet");

    // 12 拍、十六分、250 BPM、静音。
    g.cfg.beats = 12;
    g.cfg.subdiv = 4;
    g.cfg.bpm = 250;
    g.cfg.volume = 0;
    mn_ui_update(&g);
    beat(11, 0, 23, 0.0f);
    beat(11, 1, 23, 0.1f);
    capture("05_main_12beats_mute");

    // 设置面板：在运行中打开。
    reset_model();
    g.running = true;
    mn_ui_update(&g);
    key(MN_BTN_OK, MN_EV_LONG);
    run(300);
    beat(0, 0, 40, 0.05f);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    capture("06_settings_browse");
    key(MN_BTN_DOWN, MN_EV_PRESS);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    key(MN_BTN_OK, MN_EV_CLICK);
    key(MN_BTN_DOWN, MN_EV_PRESS);
    capture("07_settings_edit_volume");
    key(MN_BTN_OK, MN_EV_CLICK);
    key(MN_BTN_UP, MN_EV_PRESS);
    key(MN_BTN_UP, MN_EV_PRESS);
    key(MN_BTN_OK, MN_EV_CLICK);
    key(MN_BTN_UP, MN_EV_PRESS);
    capture("08_settings_edit_subdiv");
    key(MN_BTN_OK, MN_EV_CLICK);
    for (int i = 0; i < 4; i++) key(MN_BTN_DOWN, MN_EV_PRESS);   // 细分行 → 敲击测速行
    if (g.row != MN_ROW_TAP) {
        fprintf(stderr, "preview script: expected tap row, got %u\n", (unsigned)g.row);
        return 3;
    }
    capture("09_settings_tap_row");
    // 从设置页的"敲击测速"行进入，长按取消后应回到原样的设置面板。
    key(MN_BTN_OK, MN_EV_CLICK);
    capture("09b_tap_from_settings");
    key(MN_BTN_OK, MN_EV_PRESS);
    key(MN_BTN_OK, MN_EV_LONG);
    capture("09c_back_to_settings");
    if (g.page != MN_PAGE_SETTINGS) {
        fprintf(stderr, "preview script: expected settings page after cancel\n");
        return 3;
    }

    // 敲击测速：从主界面双击进入。
    key(MN_BTN_OK, MN_EV_LONG);
    run(100);
    key(MN_BTN_OK, MN_EV_DOUBLE);
    capture("10_tap_empty");
    for (int i = 0; i < 2; i++) {
        key(MN_BTN_OK, MN_EV_PRESS);
        run(625);
    }
    capture("11_tap_need_more");
    for (int i = 0; i < 4; i++) {
        key(MN_BTN_OK, MN_EV_PRESS);
        if (i < 3) run(625);
    }
    run(60);
    capture("12_tap_counting");
    run(MN_TAP_APPLY_IDLE_MS);
    tick();
    capture("13_tap_done");
    run(MN_TAP_DONE_SHOW_MS + 40);
    tick();
    capture("14_back_to_main");

    // 异常：音频不可用 + 低电量。
    lv_obj_t *old = lv_screen_active();
    reset_model();
    mn_ui_init(false);
    lv_obj_delete(old);
    mn_ui_set_battery(15);
    mn_ui_show(&g);
    capture("15_audio_fail_low_battery");
    mn_ui_lowbatt_notice(true);
    capture("16_low_battery_notice");
    mn_ui_lowbatt_notice(false);

    printf("PEAK used=%zu of %u\n", s_peak, (unsigned)LV_MEM_SIZE);
    return missing ? 2 : 0;
}
