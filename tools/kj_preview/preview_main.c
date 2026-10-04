// tools/kj_preview/preview_main.c —— 在电脑上用真实 LVGL 渲染限定猜拳的各个页面。
//
// 由 tools/render_kj_preview.py 编译运行。它链接固件里的规则引擎、协议、界面状态机、模型组装与
// 界面代码，用一台内存里的"庄家"和两台"选手"（A、B）通过无丢包的模拟信道真实走完一局：
// 找赌局 → 入座 → 开局 → 挑战 → 应战 → 出牌 → 亮牌 → 终局，并逐页输出 PPM，
// 同时报告 LVGL 内存池峰值与字形自检结果。
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "kj_client.h"
#include "kj_flow.h"
#include "kj_fonts.h"
#include "kj_model.h"
#include "kj_server.h"
#include "kj_ui.h"

#define W KJ_UI_W
#define H KJ_UI_H

static uint16_t s_frame[W * H];
static uint8_t s_render_buf[W * H * 2];
static uint32_t s_ms = 1000;
static const char *s_out = ".";
static size_t s_peak;
static int s_failures;

static kj_server_t srv;
static kj_client_t ca, cb;
static kj_flow_t fa, fb;
static const uint8_t MAC_H[6] = { 0x24, 0x6F, 0x28, 0x5E, 0xA3, 0xF2 };
static const uint8_t MAC_A[6] = { 0x24, 0x6F, 0x28, 0x11, 0x22, 0x07 };
static const uint8_t MAC_B[6] = { 0x24, 0x6F, 0x28, 0x11, 0x22, 0x0C };
static bool s_mute_a;   // 模拟 A 走远听不到庄家

static uint32_t tick_cb(void) { return s_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&s_frame[y * W + area->x1], src, (size_t)w * 2);
        src += w;
    }
    lv_display_flush_ready(d);
}

static void route(const kj_outbox_t *o, const uint8_t *src);

static void to_server(const uint8_t *src, const kj_out_t *it, int8_t rssi)
{
    kj_outbox_t reply;
    kj_outbox_clear(&reply);
    kj_server_on_frame(&srv, src, rssi, it->data, it->len, s_ms, &reply);
    route(&reply, MAC_H);
}

static void route(const kj_outbox_t *o, const uint8_t *src)
{
    for (int k = 0; k < o->count; k++) {
        const kj_out_t *it = &o->items[k];
        bool from_a = memcmp(src, MAC_A, 6) == 0;
        if (memcmp(src, MAC_H, 6) != 0 && (it->broadcast || memcmp(it->mac, MAC_H, 6) == 0)) {
            if (!(from_a && s_mute_a)) to_server(src, it, -48);
        }
        if (memcmp(src, MAC_A, 6) != 0 && (it->broadcast || memcmp(it->mac, MAC_A, 6) == 0)) {
            if (!(s_mute_a && memcmp(src, MAC_H, 6) == 0)) kj_client_on_frame(&ca, src, -46,
                                                                              it->data, it->len, s_ms);
        }
        if (memcmp(src, MAC_B, 6) != 0 && (it->broadcast || memcmp(it->mac, MAC_B, 6) == 0)) {
            kj_client_on_frame(&cb, src, -52, it->data, it->len, s_ms);
        }
    }
}

static void step_net(void)
{
    kj_outbox_t o;
    kj_outbox_clear(&o);
    kj_server_tick(&srv, s_ms, &o);
    route(&o, MAC_H);
    kj_outbox_clear(&o);
    kj_client_tick(&ca, s_ms, &o);
    route(&o, MAC_A);
    kj_outbox_clear(&o);
    kj_client_tick(&cb, s_ms, &o);
    route(&o, MAC_B);
    kj_event_t e;
    while (kj_rules_pop_event(&srv.game, &e)) {
    }
}

static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 20) {
        s_ms += 20;
        step_net();
        lv_timer_handler();
    }
}

typedef struct {
    kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];
    kj_opponent_t opps[KJ_UI_OPP_MAX];
    kj_player_ctx_t ctx;
} ctx_buf_t;

static const kj_player_ctx_t *make_ctx(ctx_buf_t *b, const kj_client_t *c)
{
    b->ctx.client = c;
    b->ctx.rooms = b->rooms;
    b->ctx.room_count = kj_client_rooms(c, s_ms, b->rooms, KJ_CLIENT_MAX_ROOMS);
    b->ctx.opps = b->opps;
    b->ctx.opp_count = kj_client_opponents(c, s_ms, b->opps, KJ_UI_OPP_MAX);
    b->ctx.connected = kj_client_connected(c, s_ms);
    b->ctx.now_ms = s_ms;
    return &b->ctx;
}

static void write_ppm(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", s_out, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = s_frame[i];
        int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        fputc((r << 3) | (r >> 2), f);
        fputc((g << 2) | (g >> 4), f);
        fputc((b << 3) | (b >> 2), f);
    }
    fclose(f);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    if (mon.max_used > s_peak) s_peak = mon.max_used;
    printf("MEM %-24s used=%6zu peak=%6zu largest_free=%6zu\n", name, (size_t)(mon.total_size - mon.free_size),
           (size_t)mon.max_used, (size_t)mon.free_biggest_size);
}

static void show(const kj_ui_model_t *m, const char *name)
{
    kj_ui_render(m);
    lv_refr_now(NULL);
    write_ppm(name);
}

static kj_page_t player_page(kj_flow_t *f, const kj_client_t *c, kj_ui_model_t *m)
{
    ctx_buf_t b;
    const kj_player_ctx_t *ctx = make_ctx(&b, c);
    kj_page_t page = kj_flow_player_update(f, ctx, NULL);
    kj_model_player(m, f, ctx, page, 76);
    return page;
}

static void shot_player(kj_flow_t *f, kj_client_t *c, const char *name, kj_page_t expect)
{
    kj_ui_model_t m;
    kj_page_t page = player_page(f, c, &m);
    if (page != expect) {
        fprintf(stderr, "%s: expected page %d, got %d\n", name, expect, page);
        s_failures++;
    }
    show(&m, name);
}

static void press(kj_flow_t *f, kj_client_t *c, kj_key_t key)
{
    ctx_buf_t b;
    const kj_player_ctx_t *ctx = make_ctx(&b, c);
    kj_flow_player_update(f, ctx, NULL);
    kj_action_t a = kj_flow_player_key(f, ctx, key);
    if (a.kind == KJ_ACT_JOIN) kj_client_join(c, a.room, s_ms);
    if (a.kind == KJ_ACT_REQUEST && !kj_client_request(c, a.op, a.arg, s_ms)) {
        fprintf(stderr, "request op=%u rejected locally\n", a.op);
        s_failures++;
    }
}

static void shot_host(const char *name)
{
    kj_ui_model_t m;
    if (fa.host_confirm < 0) kj_flow_host_sync(&fa, &srv.game);
    kj_model_host(&m, &fa, &srv, s_ms, 76, true);
    show(&m, name);
}

static void missing_cb(const char *font, uint32_t cp)
{
    printf("FONT-MISSING %s U+%04X\n", font, (unsigned)cp);
}

static void set_final(int idx, uint8_t status, uint8_t stars, uint8_t reason)
{
    kj_player_t *p = &srv.game.players[idx];
    memset(p->cards, 0, sizeof(p->cards));
    if (reason == KJ_FINAL_TIME_UP) p->cards[KJ_ROCK] = 2;
    p->stars = stars;
    p->status = status;
    p->final_reason = reason;
    kj_rules_touch(&srv.game, idx);
}

int main(int argc, char **argv)
{
    if (argc > 1) s_out = argv[1];
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_buffers(d, s_render_buf, NULL, sizeof(s_render_buf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);

    int missing = kj_fonts_selfcheck(missing_cb);
    printf("FONT missing=%d\n", missing);

    kj_ui_init();
    kj_server_init(&srv, 0xA3F2, 7);
    kj_client_init(&ca, 11);
    kj_client_init(&cb, 22);
    kj_flow_init(&fa, 0);
    kj_flow_init(&fb, 0);

    kj_ui_model_t m;
    kj_model_title(&m, &fa, 76, s_ms);
    show(&m, "01_title");
    fa.title_sel = 1;
    kj_model_title(&m, &fa, 76, s_ms);
    show(&m, "02_title_host");
    fa.title_sel = 0;

    shot_player(&fa, &ca, "03_rooms_searching", KJ_PAGE_ROOMS);
    run(1200);
    shot_player(&fa, &ca, "04_rooms_found", KJ_PAGE_ROOMS);
    press(&fa, &ca, KJ_KEY_OK);
    shot_player(&fa, &ca, "05_joining", KJ_PAGE_ROOMS);
    run(200);
    // B 与两个电脑选手入座
    press(&fb, &cb, KJ_KEY_OK);
    kj_server_command(&srv, KJ_CMD_BOT_ADD, 0, s_ms);
    kj_server_command(&srv, KJ_CMD_BOT_ADD, 0, s_ms);
    run(1600);
    shot_player(&fa, &ca, "06_seat", KJ_PAGE_SEAT);
    shot_host("07_host_lobby");

    kj_server_command(&srv, KJ_CMD_START, 0, s_ms);
    run(1600);
    shot_player(&fa, &ca, "08_hand", KJ_PAGE_HAND);
    press(&fa, &ca, KJ_KEY_OK);
    shot_player(&fa, &ca, "09_opponents", KJ_PAGE_OPPONENTS);
    // 选中 B（附近信号最强，排在第一）发起挑战
    press(&fa, &ca, KJ_KEY_OK);
    run(3000);
    shot_player(&fa, &ca, "10_wait", KJ_PAGE_WAIT);
    shot_player(&fb, &cb, "11_challenged", KJ_PAGE_CHALLENGED);
    press(&fb, &cb, KJ_KEY_OK);
    run(200);
    shot_player(&fa, &ca, "12_choose", KJ_PAGE_CHOOSE);
    press(&fa, &ca, KJ_KEY_DOWN);   // 剪刀 → 布
    run(100);
    press(&fa, &ca, KJ_KEY_OK);
    run(200);
    shot_player(&fa, &ca, "13_choose_locked", KJ_PAGE_CHOOSE);
    shot_player(&fb, &cb, "14_choose_peer_locked", KJ_PAGE_CHOOSE);
    press(&fb, &cb, KJ_KEY_UP);     // 剪刀 → 石头
    press(&fb, &cb, KJ_KEY_OK);
    run(200);
    shot_player(&fa, &ca, "15_reveal_win", KJ_PAGE_REVEAL);
    shot_player(&fb, &cb, "16_reveal_lose", KJ_PAGE_REVEAL);
    press(&fa, &ca, KJ_KEY_OK);
    press(&fb, &cb, KJ_KEY_OK);
    run(100);
    shot_player(&fa, &ca, "17_hand_after", KJ_PAGE_HAND);

    // 平局：B 挑战 A，双方都出剪刀；A 拒绝的提示也顺便截一张
    press(&fb, &cb, KJ_KEY_OK);
    press(&fb, &cb, KJ_KEY_OK);   // 列表第一位是 A（附近）
    run(200);
    press(&fa, &ca, KJ_KEY_OK_LONG);   // 拒绝
    run(200);
    shot_player(&fb, &cb, "18_toast_declined", KJ_PAGE_OPPONENTS);
    run(3000);
    press(&fb, &cb, KJ_KEY_OK);
    run(200);
    press(&fa, &ca, KJ_KEY_OK);
    run(200);
    press(&fa, &ca, KJ_KEY_OK);
    press(&fb, &cb, KJ_KEY_OK);
    run(200);
    shot_player(&fa, &ca, "19_reveal_draw", KJ_PAGE_REVEAL);
    press(&fa, &ca, KJ_KEY_OK);
    press(&fb, &cb, KJ_KEY_OK);
    run(100);
    shot_host("20_host_running");
    fa.host_sel = KJ_HM_END;
    fa.host_confirm = KJ_HM_END;
    shot_host("21_host_confirm");
    fa.host_confirm = -1;
    fa.host_sel = 0;

    // 终局三种
    int ia = kj_rules_find_mac(&srv.game, MAC_A);
    int ib = kj_rules_find_mac(&srv.game, MAC_B);
    set_final(ia, KJ_ST_CLEARED, 5, KJ_FINAL_NONE);
    set_final(ib, KJ_ST_ELIMINATED, 0, KJ_FINAL_NONE);
    run(300);
    shot_player(&fa, &ca, "22_final_cleared", KJ_PAGE_FINAL);
    shot_player(&fb, &cb, "23_final_out", KJ_PAGE_FINAL);
    set_final(ia, KJ_ST_FAILED, 2, KJ_FINAL_TIME_UP);
    run(300);
    shot_player(&fa, &ca, "24_final_failed", KJ_PAGE_FINAL);

    // 失联：A 听不到庄家 6 秒
    kj_server_command(&srv, KJ_CMD_NEW_GAME, 0, s_ms);
    run(500);
    s_mute_a = true;
    run(6000);
    shot_player(&fa, &ca, "25_seat_disconnected", KJ_PAGE_SEAT);

    printf("PEAK used=%zu of %u\n", s_peak, (unsigned)LV_MEM_SIZE);
    if (missing) s_failures++;
    return s_failures ? 1 : 0;
}
