// main/mn_app.c —— 应用任务：串行处理按键与周期事件，执行状态机产生的副作用。
//
// 线程模型：
//   按键回调（esp_timer 任务）── 入队（附 µs 时间戳）──> 应用任务（优先级 5）：唯一修改
//                                                       mn_model_t 的地方，持 bsp_lvgl_lock() 更新界面
//   音频任务（优先级 6）── 节拍事件 ──> LVGL 任务（优先级 4）的 lv_timer：到点点亮节拍、驱动摆杆
#include "mn_app.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "mn_audio.h"
#include "mn_fonts.h"
#include "mn_model.h"
#include "mn_store.h"
#include "mn_ui.h"

static const char *TAG = "mn_app";

#define APP_TASK_STACK 6144
#define APP_TASK_PRIORITY 5
#define QUEUE_DEPTH 16
#define TICK_MAX_MS 200
#define BATTERY_PERIOD_MS 10000
#define LVGL_LOCK_MS 200
#define ANIM_PERIOD_MS 20
#define STORE_QUIET_MS 300

_Static_assert((int)MN_BTN_UP == (int)BSP_BTN_UP && (int)MN_BTN_DOWN == (int)BSP_BTN_DOWN &&
                   (int)MN_BTN_OK == (int)BSP_BTN_OK, "按键枚举需与 BSP 一致");
_Static_assert((int)MN_EV_PRESS == (int)BSP_BTN_PRESS && (int)MN_EV_CLICK == (int)BSP_BTN_CLICK &&
                   (int)MN_EV_DOUBLE == (int)BSP_BTN_DOUBLE && (int)MN_EV_LONG == (int)BSP_BTN_LONG &&
                   (int)MN_EV_RELEASE == (int)BSP_BTN_RELEASE,
               "按键事件枚举需与 BSP 一致");

typedef struct {
    int64_t t_us;   // 按键回调里记录的时间（敲击测速用）
    uint8_t btn;
    uint8_t ev;
} msg_t;

static QueueHandle_t s_queue;
static mn_model_t s_model;   // 只在应用任务里读写（启动时除外）
static mn_power_t s_power;
static bool s_battery_ok;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---- 回调：只入队 ----

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    const msg_t msg = { .t_us = esp_timer_get_time(), .btn = (uint8_t)btn, .ev = (uint8_t)ev };
    xQueueSend(s_queue, &msg, 0);
}

// LVGL 任务内运行（已持有 LVGL 锁）：取出到点的节拍事件，更新动画。
static void anim_cb(lv_timer_t *timer) {
    (void)timer;
    const int64_t now = esp_timer_get_time();
    mn_beat_t b;
    while (mn_audio_pop(now, &b)) mn_ui_beat(&b, now);
    mn_ui_animate(now);
}

// ---- 副作用 ----

static void apply(mn_fx_t fx) {
    if (fx & (MN_FX_RUN | MN_FX_METER | MN_FX_SOUND | MN_FX_VOLUME)) {
        mn_audio_set(&s_model.cfg, s_model.running);
    }
    if (fx & MN_FX_SAVE) mn_store_mark(now_ms());
    if (!(fx & (MN_FX_SCREEN | MN_FX_REFRESH | MN_FX_RUN | MN_FX_TAP_FLASH))) return;
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) {
        ESP_LOGW(TAG, "LVGL 锁超时，本次界面刷新跳过");
        return;
    }
    if (fx & MN_FX_SCREEN) mn_ui_show(&s_model);
    else mn_ui_update(&s_model);
    if (fx & MN_FX_TAP_FLASH) mn_ui_tap_flash(esp_timer_get_time());
    bsp_lvgl_unlock();
}

static void handle(const msg_t *msg) {
    const mn_btn_t btn = (mn_btn_t)msg->btn;
    const mn_ev_t ev = (mn_ev_t)msg->ev;
    const uint32_t now = now_ms();
    const bool was_dimmed = s_power.dimmed;
    if (mn_power_key(&s_power, btn, ev, now)) {
        if (was_dimmed) bsp_display_backlight(mn_power_backlight(&s_power));
        return;
    }
    apply(mn_model_key(&s_model, btn, ev, now, msg->t_us));
}

static void update_battery(void) {
    const int soc = s_battery_ok ? bsp_battery_soc() : -1;
    if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
        mn_ui_set_battery(soc);
        bsp_lvgl_unlock();
    }
}

static void app_task(void *arg) {
    (void)arg;
    uint32_t last_battery = now_ms();
    for (;;) {
        msg_t msg;
        const uint32_t wait = mn_model_wait_ms(&s_model, now_ms(), TICK_MAX_MS);
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(wait ? wait : 1)) == pdTRUE) handle(&msg);

        const uint32_t now = now_ms();
        apply(mn_model_tick(&s_model, now));
        if (mn_power_tick(&s_power, now, s_model.running)) {
            bsp_display_backlight(mn_power_backlight(&s_power));
        }
        mn_store_tick(&s_model.cfg, now, !s_model.running && mn_audio_quiet(STORE_QUIET_MS));
        if (now - last_battery >= BATTERY_PERIOD_MS) {
            last_battery = now;
            update_battery();
        }
    }
}

void mn_app_start(bool audio_ok, bool battery_ok) {
    s_battery_ok = battery_ok;

    mn_cfg_t cfg;
    mn_store_init(&cfg);
    mn_model_init(&s_model, &cfg);
    mn_power_init(&s_power, now_ms());

    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(msg_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "创建事件队列失败");
        return;
    }
    if (!mn_audio_start(audio_ok)) ESP_LOGE(TAG, "音频任务启动失败，节拍器无法运行");
    mn_audio_set(&s_model.cfg, false);

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败，界面无法建立");
    } else {
        const int missing = mn_fonts_selfcheck();
        if (missing) ESP_LOGE(TAG, "有 %d 个字形缺失，界面可能出现方块", missing);
        mn_ui_init(audio_ok);
        mn_ui_set_battery(battery_ok ? bsp_battery_soc() : -1);
        mn_ui_show(&s_model);
        lv_timer_create(anim_cb, ANIM_PERIOD_MS, NULL);
        bsp_lvgl_unlock();
    }
    bsp_display_backlight(mn_power_backlight(&s_power));

    if (xTaskCreate(app_task, "mn_app", APP_TASK_STACK, NULL, APP_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建应用任务失败");
        return;
    }
    // 按键最后接入：队列、任务、界面都就绪之后才会有事件进来。
    const esp_err_t e = bsp_button_init(on_button, NULL);
    if (e != ESP_OK) ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(e));
    ESP_LOGI(TAG, "节拍器已启动（音频 %s，电量 %s，%u BPM）", audio_ok ? "可用" : "不可用",
             battery_ok ? "可用" : "不可用", s_model.cfg.bpm);
}
