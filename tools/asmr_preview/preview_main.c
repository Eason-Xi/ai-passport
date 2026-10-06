// tools/asmr_preview/preview_main.c —— 在主机上用真实 LVGL 渲染 ASMR 声景播放器的各个页面。
// 由 tools/render_asmr_preview.py 编译运行：链接固件里的界面与状态机代码，按真实按键流程
// 驱动 as_model_t，喂入模拟的音频电平，逐页输出 PPM，并报告 LVGL 内存池占用与字形自检结果。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "as_fonts.h"
#include "as_model.h"
#include "as_theme.h"
#include "as_ui.h"

static uint16_t s_frame[AS_SCREEN_W * AS_SCREEN_H];
static uint8_t s_render_buf[AS_SCREEN_W * AS_SCREEN_H * 2];
static uint32_t s_tick_ms = 1000;
static const char *s_out_dir = ".";
static size_t s_peak;
static as_model_t g;
static uint8_t s_meters[4] = { 0, 0, 0, 0 };

static uint32_t tick_cb(void) {
    return s_tick_ms;
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    const uint16_t *src = (const uint16_t *)px_map;
    const int w = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&s_frame[y * AS_SCREEN_W + area->x1], src, (size_t)w * 2);
        src += w;
    }
    lv_display_flush_ready(display);
}

// 推进 ms 毫秒：每 40 ms 跑一次动画与 LVGL（与固件的 lv_timer 周期一致）。
static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 40) {
        s_tick_ms += 40;
        as_ui_animate(s_tick_ms, s_meters, true);
        lv_timer_handler();
    }
}

static void capture(const char *name) {
    run(400);
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", s_out_dir, name);
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fprintf(file, "P6\n%d %d\n255\n", AS_SCREEN_W, AS_SCREEN_H);
    for (int i = 0; i < AS_SCREEN_W * AS_SCREEN_H; i++) {
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

static void apply(as_fx_t fx) {
    if (fx & AS_FX_SCREEN) as_ui_show(&g, s_tick_ms);
    else if (fx & AS_FX_REFRESH) as_ui_update(&g, s_tick_ms);
}

static void key(as_btn_t b, as_ev_t e) {
    s_tick_ms += 30;
    apply(as_model_key(&g, b, e, s_tick_ms));
}

static void tick_until(uint32_t t) {
    while (s_tick_ms < t) {
        s_tick_ms += 100;
        apply(as_model_tick(&g, s_tick_ms));
    }
}

static void expect(bool ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "preview script: %s\n", what);
        exit(3);
    }
}

int main(int argc, char **argv) {
    if (argc > 1) s_out_dir = argv[1];
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *display = lv_display_create(AS_SCREEN_W, AS_SCREEN_H);
    lv_display_set_buffers(display, s_render_buf, NULL, sizeof s_render_buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush_cb);

    // 字形自检：固件启动时同一段代码；再加一个反例，保证检查本身不会无条件通过。
    int missing = as_fonts_selfcheck();
    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(&as_zh18, &dsc, 0x9F98, 0) && !dsc.is_placeholder) {
        printf("FONT-CHECK-BROKEN: U+9F98 reported as covered\n");
        missing++;
    }
    printf("FONT missing=%d\n", missing);

    as_cfg_t cfg;
    as_cfg_default(&cfg);
    as_model_init(&g, &cfg, s_tick_ms);
    as_ui_init(true);
    as_ui_set_battery(86);
    as_ui_show(&g, s_tick_ms);
    capture("01_home_paused");

    // 播放：光环随电平起伏。
    key(AS_BTN_OK, AS_EV_CLICK);
    s_meters[0] = 210;
    s_meters[1] = 120;
    s_meters[3] = 180;
    tick_until(s_tick_ms + 65000);   // 倒数约 1 分钟
    capture("02_home_playing");
    key(AS_BTN_UP, AS_EV_PRESS);
    key(AS_BTN_UP, AS_EV_PRESS);
    capture("03_home_volume");
    tick_until(s_tick_ms + 2000);

    // 菜单抽屉。
    key(AS_BTN_OK, AS_EV_LONG);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    capture("04_menu");

    // 调音台：第 3 轨选"虫鸣"，然后调音量。
    key(AS_BTN_UP, AS_EV_PRESS);
    key(AS_BTN_OK, AS_EV_CLICK);
    expect(g.page == AS_PAGE_MIXER, "expected mixer");
    key(AS_BTN_DOWN, AS_EV_PRESS);
    capture("05_mixer");
    key(AS_BTN_DOWN, AS_EV_PRESS);
    key(AS_BTN_OK, AS_EV_CLICK);
    expect(g.page == AS_PAGE_PICKER, "expected picker");
    for (int i = 0; i < 6; i++) key(AS_BTN_DOWN, AS_EV_PRESS);   // 空 → 细雨 … 虫鸣
    capture("06_picker");
    key(AS_BTN_OK, AS_EV_CLICK);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    capture("07_mixer_edit_level");
    key(AS_BTN_OK, AS_EV_CLICK);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    capture("08_mixer_done");
    key(AS_BTN_OK, AS_EV_CLICK);
    s_meters[2] = 90;
    capture("09_home_three_layers");

    // 睡眠定时。
    key(AS_BTN_OK, AS_EV_LONG);
    key(AS_BTN_DOWN, AS_EV_PRESS);
    key(AS_BTN_OK, AS_EV_CLICK);
    expect(g.page == AS_PAGE_TIMER, "expected timer");
    key(AS_BTN_DOWN, AS_EV_PRESS);
    capture("10_timer_45");
    for (int i = 0; i < 5; i++) key(AS_BTN_UP, AS_EV_PRESS);
    capture("11_timer_off");
    key(AS_BTN_OK, AS_EV_LONG);

    // 呼吸引导：吸气中、屏息、呼气中。
    key(AS_BTN_OK, AS_EV_DOUBLE);
    expect(g.page == AS_PAGE_BREATH, "expected breath");
    run(2200);
    capture("12_breath_inhale");
    run(3600);
    capture("13_breath_hold");
    run(8000);
    capture("14_breath_exhale");
    key(AS_BTN_UP, AS_EV_PRESS);
    run(600);
    capture("15_breath_even");
    key(AS_BTN_OK, AS_EV_CLICK);

    // 定时到点："晚安"画面。
    g.cfg.timer = AS_TIMER_15;
    g.timer_left_ms = 1000;
    g.timer_end_ms = s_tick_ms + 1000;
    tick_until(s_tick_ms + 1500);
    expect(g.night, "expected night");
    capture("16_goodnight");
    tick_until(s_tick_ms + AS_NIGHT_MS + 200);

    // 异常：音频不可用 + 低电量；全部轨道为空。
    lv_obj_t *old = lv_screen_active();
    as_cfg_t empty = cfg;
    for (int i = 0; i < 3; i++) empty.sound[i] = AS_SOUND_NONE;
    empty.timer = AS_TIMER_OFF;
    as_model_init(&g, &empty, s_tick_ms);
    as_ui_init(false);
    lv_obj_delete(old);
    as_ui_set_battery(12);
    as_ui_show(&g, s_tick_ms);
    capture("17_audio_fail_empty_low_battery");
    as_ui_lowbatt(true);
    capture("18_low_battery_notice");
    as_ui_lowbatt(false);

    printf("PEAK used=%zu of %u\n", s_peak, (unsigned)LV_MEM_SIZE);
    return missing ? 2 : 0;
}
