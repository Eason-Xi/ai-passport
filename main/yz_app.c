// main/yz_app.c —— 应用任务：串行处理按键与时间流逝，执行状态机返回的副作用。
//
// 线程模型：
//   按键回调（esp_timer 任务）── 入队 ──> 应用任务（prio 5）：唯一修改 yz_book_t 的地方，
//                                         持 bsp_lvgl_lock() 更新界面
//   LVGL 任务（prio 4）只负责渲染；提示音任务（prio 6）只写 PCM。
#include "yz_app.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "yz_book.h"
#include "yz_chime.h"
#include "yz_fonts.h"
#include "yz_glyph.h"
#include "yz_power.h"
#include "yz_store.h"
#include "yz_ui.h"

static const char *TAG = "yz_app";

#define APP_TASK_STACK 6144
#define APP_TASK_PRIORITY 5
#define QUEUE_DEPTH 16
#define TICK_MS 100
#define BATTERY_PERIOD_MS 10000
#define LVGL_LOCK_MS 300

_Static_assert((int)YZ_BTN_UP == (int)BSP_BTN_UP && (int)YZ_BTN_DOWN == (int)BSP_BTN_DOWN &&
                   (int)YZ_BTN_OK == (int)BSP_BTN_OK, "按键枚举需与 BSP 一致");
_Static_assert((int)YZ_EV_PRESS == (int)BSP_BTN_PRESS && (int)YZ_EV_CLICK == (int)BSP_BTN_CLICK &&
                   (int)YZ_EV_DOUBLE == (int)BSP_BTN_DOUBLE && (int)YZ_EV_LONG == (int)BSP_BTN_LONG,
               "按键事件枚举需与 BSP 一致");

// 字形包由 main/CMakeLists.txt 以 EMBED_FILES 嵌入 Flash。
extern const uint8_t yz_glyphs_start[] asm("_binary_yz_glyphs_bin_start");
extern const uint8_t yz_glyphs_end[] asm("_binary_yz_glyphs_bin_end");

typedef struct {
    uint8_t btn;
    uint8_t ev;
} msg_t;

static QueueHandle_t s_queue;
static yz_book_t s_book;            // 只在应用任务里读写（启动时除外）
static yz_power_t s_power;
static yz_glyph_pack_t s_pack;
static bool s_battery_ok;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    if (ev == BSP_BTN_PRESS) return;    // 状态机不用按下事件，省得占队列
    const msg_t msg = { .btn = (uint8_t)btn, .ev = (uint8_t)ev };
    xQueueSend(s_queue, &msg, 0);
}

static void apply_backlight(void) {
    bsp_display_backlight(yz_power_backlight(&s_power, &s_book.cfg));
}

static void apply(uint32_t fx) {
    if (!fx) return;
    if (fx & YZ_FX_CHIME) yz_chime_play();
    if (fx & YZ_FX_BRIGHT) apply_backlight();
    yz_store_mark(fx & YZ_FX_SAVE_CFG, fx & YZ_FX_SAVE_PROG, now_ms());

    if (!(fx & (YZ_FX_SCREEN | YZ_FX_REFRESH | YZ_FX_GLYPH | YZ_FX_TOAST))) return;
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) {
        ESP_LOGW(TAG, "LVGL 锁超时，本次界面刷新跳过");
        return;
    }
    if (fx & YZ_FX_SCREEN) {
        yz_ui_show(&s_book);
    } else {
        if (fx & YZ_FX_GLYPH) yz_ui_glyph(&s_book);
        if (fx & YZ_FX_REFRESH) yz_ui_update(&s_book);
        if (fx & YZ_FX_TOAST) yz_ui_toast(&s_book);
    }
    bsp_lvgl_unlock();
}

static void update_battery(void) {
    const int soc = s_battery_ok ? bsp_battery_soc() : -1;
    if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
        yz_ui_set_battery(soc);
        bsp_lvgl_unlock();
    }
}

static void app_task(void *arg) {
    (void)arg;
    uint32_t last = now_ms();
    uint32_t last_battery = last;
    for (;;) {
        msg_t msg;
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            const bool was_dark = s_power.level != YZ_POWER_ON;
            if (yz_power_key(&s_power, now_ms())) {
                if (was_dark) apply_backlight();   // 熄屏 / 调暗时第一下只唤醒
            } else {
                apply(yz_book_input(&s_book, (yz_btn_t)msg.btn, (yz_ev_t)msg.ev));
            }
        }
        const uint32_t now = now_ms();
        apply(yz_book_tick(&s_book, now - last));
        last = now;

        const bool busy = s_book.timing || yz_chime_active();
        if (yz_power_tick(&s_power, now, busy)) apply_backlight();
        yz_store_tick(&s_book.cfg, &s_book.prog, now, yz_chime_active());
        if (now - last_battery >= BATTERY_PERIOD_MS) {
            last_battery = now;
            update_battery();
        }
    }
}

void yz_app_start(bool audio_ok, bool battery_ok) {
    s_battery_ok = battery_ok;

    yz_cfg_t cfg;
    yz_progress_t prog;
    yz_store_init(&cfg, &prog);
    yz_book_init(&s_book, &cfg, &prog);
    yz_power_init(&s_power, now_ms());

    const size_t pack_size = (size_t)(yz_glyphs_end - yz_glyphs_start);
    const bool pack_ok = yz_glyph_pack_open(&s_pack, yz_glyphs_start, pack_size);
    if (!pack_ok || s_pack.count != YZ_ENTRY_COUNT) {
        ESP_LOGE(TAG, "字形包无效（%u 字节，%u 字），拓本字将显示为空白",
                 (unsigned)pack_size, (unsigned)s_pack.count);
    }

    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(msg_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "创建事件队列失败");
        return;
    }
    if (!yz_chime_start(audio_ok)) ESP_LOGW(TAG, "提示音不可用");

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败，界面无法建立");
    } else {
        const int missing = yz_fonts_selfcheck();
        if (missing) ESP_LOGE(TAG, "有 %d 个字形缺失，界面可能出现方块", missing);
        yz_ui_init(pack_ok && s_pack.count == YZ_ENTRY_COUNT ? &s_pack : NULL);
        yz_ui_set_battery(battery_ok ? bsp_battery_soc() : -1);
        yz_ui_show(&s_book);
        bsp_lvgl_unlock();
    }
    apply_backlight();

    if (xTaskCreate(app_task, "yz_app", APP_TASK_STACK, NULL, APP_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建应用任务失败");
        return;
    }
    // 按键最后接入：队列、任务、界面都就绪之后才会有事件进来。
    const esp_err_t e = bsp_button_init(on_button, NULL);
    if (e != ESP_OK) ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(e));
    ESP_LOGI(TAG, "颜真卿字帖已启动：%u 字，音频 %s，电量 %s", (unsigned)YZ_ENTRY_COUNT,
             audio_ok ? "可用" : "不可用", battery_ok ? "可用" : "不可用");
}
