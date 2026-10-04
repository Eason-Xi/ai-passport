// main/main.c —— 限定猜拳（《赌博默示录》）多人联机固件入口。
//
// 同一份固件，开机选择角色：
//   * 选手：通过 ESP-NOW 找到附近的庄家入座，用自己的设备暗中出牌；
//   * 庄家：裁判兼网关，持有唯一可信的赌局状态，并通过 USB 串口把全场手牌实时推给电脑看板
//     （tools/kj_board/index.html），看板只给庄家 / 观众看。
//
// 线程模型：按键回调（esp_timer 任务）与 ESP-NOW 接收回调（Wi-Fi 任务）只把事件拷贝进队列；
// 唯一的应用任务 kj_app 串行处理协议、规则、界面状态机，并在持锁时刷新 LVGL。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

#include "kj_board.h"
#include "kj_client.h"
#include "kj_flow.h"
#include "kj_fonts.h"
#include "kj_model.h"
#include "kj_persist.h"
#include "kj_radio.h"
#include "kj_server.h"
#include "kj_sound.h"
#include "kj_store.h"
#include "kj_ui.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "kj_app";

#define KJ_FW_VERSION       "1.0.0"
#define APP_QUEUE_DEPTH     32
#define APP_LOOP_MS         20
#define RENDER_MIN_MS       30
#define BATTERY_POLL_MS     15000
#define DIM_AFTER_MS        60000
#define DIM_PERCENT         15
#define BOARD_GAME_MS       1000
#define BOARD_HELLO_MS      5000
#define BOARD_FULL_SYNC_MS  15000
#define BOARD_LINES_PER_LOOP 6
#define PERSIST_DEBOUNCE_MS 1500

typedef enum { MSG_KEY = 1, MSG_RX } msg_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t key;
    int8_t rssi;
    uint8_t len;
    uint8_t mac[6];
    uint8_t data[KJ_FRAME_MAX];
} app_msg_t;

typedef enum { MODE_TITLE = 0, MODE_PLAYER, MODE_HOST } app_mode_t;

static QueueHandle_t s_queue;
static app_mode_t s_mode;
static kj_flow_t s_flow;
static kj_client_t s_client;
static kj_server_t *s_server;           // 只有庄家才分配（约 8 KB）
static kj_ui_model_t s_model;
static int s_battery = -1;
static bool s_battery_ok;
static uint32_t s_battery_ms;
static uint32_t s_last_activity;
static bool s_dimmed;
static uint32_t s_last_render;
static bool s_dirty = true;
static uint16_t s_auto_room;
static bool s_auto_tried;
static uint16_t s_saved_room;

// 选手侧列表缓冲
static kj_room_entry_t s_rooms[KJ_CLIENT_MAX_ROOMS];
static kj_opponent_t s_opps[KJ_UI_OPP_MAX];

// 庄家侧串口与持久化
static char s_cmd_line[64];
static size_t s_cmd_len;
static uint32_t s_board_game_ms, s_board_hello_ms, s_board_sync_ms;
static uint16_t s_board_phase_ver;
static uint32_t s_saved_rev, s_rev_changed_ms, s_seen_rev;
static uint8_t s_persist_buf[KJ_PERSIST_MAX];

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------------
// 回调（只入队）
// ---------------------------------------------------------------------------

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    int key = -1;
    if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) key = KJ_KEY_UP;
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_PRESS) key = KJ_KEY_DOWN;
    if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE)) key = KJ_KEY_OK;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) key = KJ_KEY_OK_LONG;
    if (key < 0 || !s_queue) return;
    app_msg_t m = { .kind = MSG_KEY, .key = (uint8_t)key };
    (void)xQueueSend(s_queue, &m, 0);
}

static void on_radio(const uint8_t mac[6], int8_t rssi, const uint8_t *data, int len)
{
    if (!s_queue || len <= 0 || len > KJ_FRAME_MAX) return;
    app_msg_t m = { .kind = MSG_RX, .rssi = rssi, .len = (uint8_t)len };
    memcpy(m.mac, mac, 6);
    memcpy(m.data, data, (size_t)len);
    (void)xQueueSend(s_queue, &m, 0);   // 满了丢弃：协议层会重发
}

// ---------------------------------------------------------------------------
// 发送、提示音、背光
// ---------------------------------------------------------------------------

static void flush_outbox(kj_outbox_t *o)
{
    for (int i = 0; i < o->count; i++) kj_radio_send(&o->items[i]);
    kj_outbox_clear(o);
}

static void cue(kj_cue_t c)
{
    if (c == KJ_CUE_NONE) return;
    kj_sound_play(c);
    if (c != KJ_CUE_KEY) s_last_activity = now_ms();   // 有事发生时点亮屏幕
}

static void wake_screen(void)
{
    s_last_activity = now_ms();
    if (s_dimmed) {
        s_dimmed = false;
        bsp_display_backlight(100);
    }
}

static void update_backlight(uint32_t now)
{
    if (!s_dimmed && (uint32_t)(now - s_last_activity) >= DIM_AFTER_MS) {
        s_dimmed = true;
        bsp_display_backlight(DIM_PERCENT);
    } else if (s_dimmed && (uint32_t)(now - s_last_activity) < DIM_AFTER_MS) {
        s_dimmed = false;
        bsp_display_backlight(100);
    }
}

// ---------------------------------------------------------------------------
// 角色切换
// ---------------------------------------------------------------------------

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free=%u min=%u largest=%u", when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static bool start_radio(void)
{
    if (kj_radio_started()) return true;
    log_heap("before radio");
    esp_err_t err = kj_radio_start(on_radio);
    log_heap("after radio");
    if (err != ESP_OK) {
        kj_flow_toast(&s_flow, KJ_TOAST_RADIO_FAIL, now_ms());
        cue(KJ_CUE_ERROR);
        return false;
    }
    return true;
}

static void enter_player(void)
{
    kj_client_init(&s_client, esp_random());
    s_auto_room = kj_store_get_room();
    s_saved_room = s_auto_room;
    s_auto_tried = false;
    s_mode = MODE_PLAYER;
    ESP_LOGI(TAG, "role: player (last room %04X)", s_auto_room);
}

static void board_write(const char *line, size_t n)
{
    if (!n) return;
    fwrite(line, 1, n, stdout);
    clearerr(stdout);   // 电脑未连接时写入会失败；清掉错误标志，连上后继续输出
}

static void enter_host(void)
{
    s_server = calloc(1, sizeof(kj_server_t));
    if (!s_server) {
        ESP_LOGE(TAG, "no memory for host state");
        kj_flow_toast(&s_flow, KJ_TOAST_RADIO_FAIL, now_ms());
        return;
    }
    uint8_t mac[6];
    kj_radio_get_mac(mac);
    uint16_t room = (uint16_t)((mac[4] << 8) | mac[5]);
    if (room == 0) room = 1;
    kj_server_init(s_server, room, esp_random());
    size_t n = kj_store_load_game(s_persist_buf, sizeof(s_persist_buf));
    if (n && kj_persist_load(&s_server->game, s_persist_buf, n, now_ms())) {
        kj_flow_toast(&s_flow, KJ_TOAST_RESTORED, now_ms());
        ESP_LOGI(TAG, "restored game: %d players, phase %d", kj_rules_player_count(&s_server->game),
                 s_server->game.phase);
    }
    s_saved_rev = s_seen_rev = s_server->game.rev;

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    // 安装 USB Serial/JTAG 驱动：日志改走驱动缓冲，并能非阻塞地读取看板命令。
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 512;
    cfg.tx_buffer_size = 2048;
    if (usb_serial_jtag_driver_install(&cfg) == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();
    } else {
        ESP_LOGW(TAG, "usb serial driver install failed; board commands unavailable");
    }
#else
    ESP_LOGW(TAG, "console is not USB Serial/JTAG; board commands unavailable");
#endif
    setvbuf(stdout, NULL, _IOLBF, 0);
    s_mode = MODE_HOST;
    char line[KJ_BOARD_LINE_MAX];
    board_write(line, kj_board_hello_line(room, KJ_FW_VERSION, line, sizeof(line)));
    ESP_LOGI(TAG, "role: host, room %04X", room);
    log_heap("host ready");
}

// ---------------------------------------------------------------------------
// 选手
// ---------------------------------------------------------------------------

static void player_ctx(kj_player_ctx_t *ctx, uint32_t now)
{
    ctx->client = &s_client;
    ctx->rooms = s_rooms;
    ctx->room_count = kj_client_rooms(&s_client, now, s_rooms, KJ_CLIENT_MAX_ROOMS);
    ctx->opps = s_opps;
    ctx->opp_count = kj_client_opponents(&s_client, now, s_opps, KJ_UI_OPP_MAX);
    ctx->connected = kj_client_connected(&s_client, now);
    ctx->now_ms = now;
}

static void player_key(kj_key_t key, uint32_t now)
{
    kj_player_ctx_t ctx;
    player_ctx(&ctx, now);
    // 先让状态机消化同一批里刚到的视图（亮牌 / 通知的提示音不能丢），再处理按键。
    kj_cue_t pending = KJ_CUE_NONE;
    kj_flow_player_update(&s_flow, &ctx, &pending);
    cue(pending);
    kj_action_t a = kj_flow_player_key(&s_flow, &ctx, key);
    kj_cue_t c = pending == KJ_CUE_NONE ? KJ_CUE_KEY : KJ_CUE_NONE;
    switch (a.kind) {
    case KJ_ACT_JOIN:
        s_auto_tried = true;
        kj_client_join(&s_client, a.room, now);
        break;
    case KJ_ACT_LEAVE:
        kj_client_leave(&s_client);
        kj_store_set_room(0);
        s_saved_room = 0;
        s_auto_tried = true;
        break;
    case KJ_ACT_TO_TITLE:
        kj_client_leave(&s_client);
        s_mode = MODE_TITLE;
        break;
    case KJ_ACT_REQUEST:
        if (!kj_client_request(&s_client, a.op, a.arg, now)) c = KJ_CUE_ERROR;
        else if (a.op == KJ_OP_PLAY) c = KJ_CUE_LOCK;
        break;
    default:
        break;
    }
    cue(c);
}

static void player_tick(uint32_t now)
{
    kj_outbox_t out;
    kj_outbox_clear(&out);
    kj_client_tick(&s_client, now, &out);
    flush_outbox(&out);

    uint8_t why = 0;
    if (kj_client_take_kicked(&s_client, &why)) {
        kj_flow_toast(&s_flow, why == KJ_N_FULL ? KJ_TOAST_FULL : KJ_TOAST_KICKED, now);
        cue(KJ_CUE_NOTICE);
        s_dirty = true;
    }
    if (kj_client_take_req_failed(&s_client)) {
        kj_flow_toast(&s_flow, KJ_TOAST_NO_REPLY, now);
        cue(KJ_CUE_ERROR);
        s_dirty = true;
    }
    if (kj_client_take_view_changed(&s_client)) s_dirty = true;

    kj_player_ctx_t ctx;
    player_ctx(&ctx, now);
    // 开机后自动回到上次的赌局（主动离座后不再自动入座）
    if (!s_auto_tried && s_auto_room && s_client.link == KJ_LINK_IDLE) {
        for (int i = 0; i < ctx.room_count; i++) {
            if (s_rooms[i].room == s_auto_room) {
                s_auto_tried = true;
                kj_client_join(&s_client, s_auto_room, now);
                break;
            }
        }
    }
    if (s_client.link == KJ_LINK_JOINED && s_client.room != s_saved_room) {
        s_saved_room = s_client.room;
        kj_store_set_room(s_saved_room);
    }
    kj_cue_t c = KJ_CUE_NONE;
    kj_page_t page = kj_flow_player_update(&s_flow, &ctx, &c);
    cue(c);
    kj_model_player(&s_model, &s_flow, &ctx, page, s_battery);
}

// ---------------------------------------------------------------------------
// 庄家
// ---------------------------------------------------------------------------

static void host_command(kj_cmd_t cmd, int arg, bool from_board, uint32_t now)
{
    kj_notice_t r = kj_server_command(s_server, cmd, arg, now);
    if (from_board) {
        char line[KJ_BOARD_LINE_MAX];
        board_write(line, kj_board_ack_line(cmd, arg, r, line, sizeof(line)));
    }
    if (cmd == KJ_CMD_SYNC) {
        s_board_hello_ms = 0;   // 立刻补发身份行与汇总
        s_board_game_ms = 0;
    }
    if (r != KJ_N_NONE && !from_board) {
        kj_flow_toast(&s_flow, cmd == KJ_CMD_START ? KJ_TOAST_NEED_TWO : KJ_TOAST_INVALID, now);
        cue(KJ_CUE_ERROR);
    }
    s_dirty = true;
}

static void host_poll_serial(uint32_t now)
{
    if (!usb_serial_jtag_is_driver_installed()) return;
    uint8_t buf[64];
    int n;
    while ((n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < n; i++) {
            char ch = (char)buf[i];
            if (ch == '\n' || ch == '\r') {
                if (s_cmd_len) {
                    s_cmd_line[s_cmd_len] = '\0';
                    kj_cmd_t cmd;
                    int arg;
                    if (kj_board_parse_command(s_cmd_line, &cmd, &arg)) host_command(cmd, arg, true, now);
                }
                s_cmd_len = 0;
            } else if (s_cmd_len < sizeof(s_cmd_line) - 1) {
                s_cmd_line[s_cmd_len++] = ch;
            } else {
                s_cmd_len = 0;   // 超长行丢弃
            }
        }
    }
}

static void host_board(uint32_t now)
{
    kj_game_t *g = &s_server->game;
    char line[KJ_BOARD_LINE_MAX];
    if ((uint32_t)(now - s_board_hello_ms) >= BOARD_HELLO_MS || s_board_hello_ms == 0) {
        s_board_hello_ms = now ? now : 1;
        board_write(line, kj_board_hello_line(s_server->room, KJ_FW_VERSION, line, sizeof(line)));
    }
    if ((uint32_t)(now - s_board_sync_ms) >= BOARD_FULL_SYNC_MS) {
        s_board_sync_ms = now;
        kj_rules_mark_all_dirty(g);   // 新打开的看板最多等 15 s 就能拿到全量
    }
    kj_event_t e;
    while (kj_rules_pop_event(g, &e)) board_write(line, kj_board_event_line(&e, line, sizeof(line)));
    int lines = 0;
    for (int i = 0; i < KJ_MAX_PLAYERS && lines < BOARD_LINES_PER_LOOP; i++) {
        if (!kj_rules_take_dirty(g, i)) continue;
        // 从没用过的空座位不输出（只有被移除的座位需要发 gone）
        if (!g->players[i].used && g->players[i].view_ver == 0) continue;
        board_write(line, kj_board_player_line(g, i, now, line, sizeof(line)));
        lines++;
    }
    if ((uint32_t)(now - s_board_game_ms) >= BOARD_GAME_MS || g->phase_ver != s_board_phase_ver) {
        s_board_game_ms = now;
        s_board_phase_ver = g->phase_ver;
        board_write(line, kj_board_game_line(g, s_server->room, now, line, sizeof(line)));
    }
}

static void host_persist(uint32_t now)
{
    kj_game_t *g = &s_server->game;
    if (g->rev != s_seen_rev) {
        s_seen_rev = g->rev;
        s_rev_changed_ms = now;
        s_dirty = true;
    }
    if (g->rev == s_saved_rev || (uint32_t)(now - s_rev_changed_ms) < PERSIST_DEBOUNCE_MS) return;
    size_t n = kj_persist_save(g, s_persist_buf, sizeof(s_persist_buf));
    if (n && kj_store_save_game(s_persist_buf, n) == ESP_OK) {
        s_saved_rev = g->rev;
    } else {
        s_saved_rev = g->rev;   // 写失败也不反复重试刷 Flash；下次变化再写
        ESP_LOGW(TAG, "game snapshot save failed");
    }
}

static void host_key(kj_key_t key, uint32_t now)
{
    kj_flow_host_sync(&s_flow, &s_server->game);
    kj_action_t a = kj_flow_host_key(&s_flow, &s_server->game, key, now);
    if (a.kind == KJ_ACT_HOST_CMD) {
        host_command(a.cmd, 0, false, now);
        cue(KJ_CUE_DUEL);
    } else {
        cue(a.arg ? KJ_CUE_ERROR : KJ_CUE_KEY);
    }
}

static void host_tick(uint32_t now)
{
    host_poll_serial(now);
    kj_outbox_t out;
    kj_outbox_clear(&out);
    kj_server_tick(s_server, now, &out);
    flush_outbox(&out);
    host_board(now);
    host_persist(now);
    kj_flow_host_sync(&s_flow, &s_server->game);
    kj_model_host(&s_model, &s_flow, s_server, now, s_battery, usb_serial_jtag_is_connected());
}

static void host_rx(const app_msg_t *m, uint32_t now)
{
    kj_outbox_t out;
    kj_outbox_clear(&out);
    kj_server_on_frame(s_server, m->mac, m->rssi, m->data, m->len, now, &out);
    flush_outbox(&out);
}

// ---------------------------------------------------------------------------
// 应用任务
// ---------------------------------------------------------------------------

static void handle_key(kj_key_t key, uint32_t now)
{
    if (s_dimmed) {   // 熄屏时第一下按键只负责点亮
        wake_screen();
        s_dirty = true;
        return;
    }
    wake_screen();
    s_dirty = true;
    switch (s_mode) {
    case MODE_TITLE: {
        kj_action_t a = kj_flow_title_key(&s_flow, key);
        cue(KJ_CUE_KEY);
        if (a.kind == KJ_ACT_ROLE && start_radio()) {
            kj_store_set_role(a.arg);
            if (a.arg) {
                enter_host();
            } else {
                enter_player();
            }
        }
        break;
    }
    case MODE_PLAYER: player_key(key, now); break;
    case MODE_HOST: if (s_server) host_key(key, now); break;
    }
}

static void app_task(void *arg)
{
    (void)arg;
    s_last_activity = now_ms();
    for (;;) {
        app_msg_t msg;
        TickType_t wait = pdMS_TO_TICKS(APP_LOOP_MS);
        while (xQueueReceive(s_queue, &msg, wait) == pdTRUE) {
            wait = 0;
            uint32_t now = now_ms();
            if (msg.kind == MSG_KEY) {
                handle_key((kj_key_t)msg.key, now);
            } else if (msg.kind == MSG_RX) {
                if (s_mode == MODE_PLAYER) {
                    kj_client_on_frame(&s_client, msg.mac, msg.rssi, msg.data, msg.len, now);
                } else if (s_mode == MODE_HOST && s_server) {
                    host_rx(&msg, now);
                }
            }
        }
        uint32_t now = now_ms();
        if (s_battery_ok && (s_battery_ms == 0 || (uint32_t)(now - s_battery_ms) >= BATTERY_POLL_MS)) {
            s_battery_ms = now ? now : 1;
            s_battery = bsp_battery_soc();
        }
        switch (s_mode) {
        case MODE_TITLE: kj_model_title(&s_model, &s_flow, s_battery, now); break;
        case MODE_PLAYER: player_tick(now); break;
        case MODE_HOST:
            if (s_server) {
                host_tick(now);
            } else {
                s_mode = MODE_TITLE;
            }
            break;
        }
        update_backlight(now);
        if ((uint32_t)(now - s_last_render) >= RENDER_MIN_MS || s_dirty) {
            if (bsp_lvgl_lock(100)) {
                kj_ui_render(&s_model);
                bsp_lvgl_unlock();
                s_last_render = now;
                s_dirty = false;
            }
        }
    }
}

static void font_missing(const char *font, uint32_t cp)
{
    ESP_LOGE(TAG, "font %s lacks U+%04X", font, (unsigned)cp);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Limited RPS %s starting", KJ_FW_VERSION);
    kj_store_init();   // 射频校准缓存也依赖 NVS
    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    s_queue = xQueueCreate(APP_QUEUE_DEPTH, sizeof(app_msg_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "no memory for event queue");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) ESP_LOGE(TAG, "button init failed");
    if (bsp_audio_init() == ESP_OK) {
        if (kj_sound_init() != ESP_OK) ESP_LOGW(TAG, "sound unavailable");
    } else {
        ESP_LOGW(TAG, "audio init failed; running silent");
    }
    s_battery_ok = bsp_battery_init() == ESP_OK;

    kj_flow_init(&s_flow, kj_store_get_role());
    kj_model_title(&s_model, &s_flow, -1, now_ms());
    if (bsp_lvgl_lock(1000)) {
        int missing = kj_fonts_selfcheck(font_missing);
        if (missing) ESP_LOGE(TAG, "font self-check: %d missing glyph(s)", missing);
        else ESP_LOGI(TAG, "font self-check: all glyphs present");
        kj_ui_init();
        kj_ui_render(&s_model);
        bsp_lvgl_unlock();
    }
    bsp_display_backlight(100);
    log_heap("ui ready");
    if (xTaskCreate(app_task, "kj_app", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create app task");
    }
}
