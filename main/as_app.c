// main/as_app.c —— 应用任务：串行处理按键与周期事件，执行状态机产生的副作用。
//
// 线程模型：
//   按键回调（esp_timer 任务）── 入队 ──> 应用任务（优先级 5）：唯一修改 as_model_t 的地方，
//                                         持 bsp_lvgl_lock() 更新界面
//   音频任务（优先级 6）── 电平 ──> LVGL 任务（优先级 4）的 lv_timer：光环 / 呼吸动画
//
// 省电与关机见 as_power.h；关机 = 关闭各外设后进入深度睡眠，任意键（按键节点低电平）唤醒，
// 唤醒即重新开机。设备的独立电源键由硬件控制，固件无法真正断电。
#include "as_app.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "as_audio.h"
#include "as_dsp.h"
#include "as_fonts.h"
#include "as_model.h"
#include "as_power.h"
#include "as_store.h"
#include "as_ui.h"

static const char *TAG = "as_app";

#define APP_TASK_STACK 6144
#define APP_TASK_PRIORITY 5
#define QUEUE_DEPTH 16
#define BATTERY_PERIOD_MS 10000
#define LVGL_LOCK_MS 200
#define ANIM_PERIOD_MS 40
#define STORE_QUIET_MS 300
#define QUIET_WAIT_MS 2500        // 关机前等待淡出与最后一块声音播完的上限
#define AUDIO_HALT_MS 500

_Static_assert((int)AS_BTN_UP == (int)BSP_BTN_UP && (int)AS_BTN_DOWN == (int)BSP_BTN_DOWN &&
                   (int)AS_BTN_OK == (int)BSP_BTN_OK, "按键枚举需与 BSP 一致");
_Static_assert((int)AS_EV_PRESS == (int)BSP_BTN_PRESS && (int)AS_EV_CLICK == (int)BSP_BTN_CLICK &&
                   (int)AS_EV_DOUBLE == (int)BSP_BTN_DOUBLE && (int)AS_EV_LONG == (int)BSP_BTN_LONG &&
                   (int)AS_EV_RELEASE == (int)BSP_BTN_RELEASE,
               "按键事件枚举需与 BSP 一致");

typedef struct {
    uint8_t btn;
    uint8_t ev;
} msg_t;

static QueueHandle_t s_queue;
static as_model_t s_model;   // 只在应用任务里读写（启动时除外）
static as_power_t s_power;
static as_lowbatt_t s_lowbatt;
static bool s_battery_ok;
static volatile bool s_screen_on = true;   // LVGL 动画据此决定是否跳过

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// ---- 回调：只入队 ----

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    const msg_t msg = { .btn = (uint8_t)btn, .ev = (uint8_t)ev };
    xQueueSend(s_queue, &msg, 0);
}

// LVGL 任务内运行（已持有 LVGL 锁）。
static void anim_cb(lv_timer_t *timer) {
    (void)timer;
    uint8_t meters[4];
    as_audio_meters(meters);
    as_ui_animate(now_ms(), meters, s_screen_on);
}

// ---- 副作用 ----

static void set_backlight(uint8_t level) {
    s_screen_on = level > 0;
    bsp_display_backlight(level);
}

static void apply(as_fx_t fx) {
    if (fx & (AS_FX_AUDIO | AS_FX_VOLUME)) as_audio_set(&s_model.cfg, s_model.playing, s_model.fade);
    if (fx & AS_FX_SAVE) as_store_mark(now_ms());
    if (fx & AS_FX_NIGHT) {
        // 定时结束：亮屏显示"晚安"，随后由 AS_FX_NIGHT_END 熄屏。
        as_power_wake(&s_power, now_ms());
        set_backlight(AS_BACKLIGHT_DIM);
    }
    if (fx & (AS_FX_SCREEN | AS_FX_REFRESH)) {
        if (!bsp_lvgl_lock(LVGL_LOCK_MS)) {
            ESP_LOGW(TAG, "LVGL 锁超时，本次界面刷新跳过");
        } else {
            if (fx & AS_FX_SCREEN) as_ui_show(&s_model, now_ms());
            else as_ui_update(&s_model, now_ms());
            bsp_lvgl_unlock();
        }
    }
    if (fx & AS_FX_NIGHT_END) {
        as_power_force(&s_power, AS_POWER_SCREEN_OFF, now_ms());
        set_backlight(AS_BACKLIGHT_OFF);
    }
}

static void handle(const msg_t *msg) {
    const uint32_t now = now_ms();
    const bool was_asleep = s_power.level != AS_POWER_ON;
    if (as_power_key(&s_power, msg->btn, msg->ev, now)) {
        if (was_asleep && s_power.level == AS_POWER_ON) set_backlight(as_power_backlight(&s_power));
        return;
    }
    const bool was_night = s_model.night;
    apply(as_model_key(&s_model, (as_btn_t)msg->btn, (as_ev_t)msg->ev, now));
    // 在"晚安"画面上按键返回聆听页：恢复正常亮度。
    if (was_night && !s_model.night) set_backlight(as_power_backlight(&s_power));
}

// 关机步骤失败只记录日志，继续后续步骤（与基线 deep sleep 流程一致）。
static void log_step(const char *step, esp_err_t e) {
    if (e != ESP_OK) ESP_LOGW(TAG, "关机步骤 %s 失败：%s", step, esp_err_to_name(e));
}

// 放弃本次关机：恢复按键与屏幕，回到正常使用。
static void cancel_power_off(const char *why) {
    ESP_LOGW(TAG, "取消自动关机：%s", why);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    const esp_err_t e = bsp_button_init(on_button, NULL);
    if (e != ESP_OK) ESP_LOGE(TAG, "恢复按键失败：%s", esp_err_to_name(e));
    as_power_wake(&s_power, now_ms());
    if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
        as_ui_lowbatt(false);
        as_ui_show(&s_model, now_ms());
        bsp_lvgl_unlock();
    }
    set_backlight(as_power_backlight(&s_power));
}

static void stop_playback(void) {
    if (!s_model.playing) return;
    s_model.playing = false;
    as_audio_set(&s_model.cfg, false, s_model.fade);
}

// 自动关机：停止播放并保存设置，释放按键节点作为唤醒源，依次关闭外设后进入深度睡眠。
// 在应用任务中阻塞执行；成功时不返回（唤醒即重启应用）。有键按住或无法配置唤醒源时放弃，
// 避免"睡了按不醒"。
static void power_off(const char *reason) {
    ESP_LOGI(TAG, "自动关机：%s", reason);
    stop_playback();
    for (uint32_t waited = 0; !as_audio_quiet(150) && waited < QUIET_WAIT_MS; waited += 50) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    as_store_flush(&s_model.cfg);

    // ADC 占着按键节点时数字电平恒为 0，必须先释放，否则入睡即醒。
    int level = 1;
    esp_err_t e = bsp_button_prepare_deep_sleep(&level);
    if (e != ESP_OK) {
        cancel_power_off(esp_err_to_name(e));
        return;
    }
    if (level == 0) {
        cancel_power_off("有按键按住");
        return;
    }
    e = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (e != ESP_OK) {
        cancel_power_off(esp_err_to_name(e));
        return;
    }
    if (!as_audio_halt(AUDIO_HALT_MS)) ESP_LOGW(TAG, "音频任务未确认停手，继续关机");

    // CW2017 与 ES8311 共用 I2C：先完成电量计写入 / 回读，再暂停 codec、释放总线引脚。
    log_step("CW2017 suspend", bsp_battery_sleep());
    log_step("ES8311 suspend", bsp_audio_sleep());
    log_step("I2S pin release", bsp_audio_prepare_deep_sleep());
    log_step("shared I2C pin release", bsp_i2c_prepare_deep_sleep());
    // 持锁等待当前 flush 结束并阻止 LVGL 再刷屏，然后关闭面板、保持 LCD 安全电平。
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "无法停止 LVGL 刷屏，重启恢复外设");
        esp_restart();
    }
    log_step("ST7789 suspend", bsp_display_prepare_deep_sleep());
    esp_deep_sleep_start();
    // 外设引脚已释放，本次运行无法恢复：意外返回时重启。
    ESP_LOGE(TAG, "esp_deep_sleep_start 意外返回，重启");
    esp_restart();
}

// 低电量：停止播放、亮屏显示提示 3 s，然后关机。
static void low_battery_power_off(void) {
    ESP_LOGW(TAG, "电量过低，准备关机");
    stop_playback();
    as_power_wake(&s_power, now_ms());
    if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
        as_ui_lowbatt(true);
        bsp_lvgl_unlock();
    }
    set_backlight(AS_BACKLIGHT_ON);
    vTaskDelay(pdMS_TO_TICKS(AS_LOWBATT_NOTICE_MS));
    power_off("电量过低");
}

static void update_battery(void) {
    const int soc = s_battery_ok ? bsp_battery_soc() : -1;
    const int mv = s_battery_ok ? bsp_battery_mv() : -1;
    if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
        as_ui_set_battery(soc);
        bsp_lvgl_unlock();
    }
    // 接电脑 USB 时视为外部供电；只接充电头则靠"电压是否上升"判断（见 as_lowbatt_feed）。
    if (as_lowbatt_feed(&s_lowbatt, soc, mv, usb_serial_jtag_is_connected())) low_battery_power_off();
}

static as_activity_t activity(uint32_t now) {
    if (as_model_keep_screen(&s_model, now)) return AS_ACT_KEEP_ON;
    return s_model.playing ? AS_ACT_PLAYING : AS_ACT_STOPPED;
}

static void app_task(void *arg) {
    (void)arg;
    uint32_t last_battery = now_ms();
    for (;;) {
        msg_t msg;
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(as_model_wait_ms(&s_model))) == pdTRUE) handle(&msg);

        const uint32_t now = now_ms();
        apply(as_model_tick(&s_model, now));
        if (as_power_tick(&s_power, now, activity(now))) {
            if (s_power.level == AS_POWER_SHUTDOWN) power_off("停止后 10 分钟无操作");
            else set_backlight(as_power_backlight(&s_power));
        }
        as_store_tick(&s_model.cfg, now, !s_model.playing && as_audio_quiet(STORE_QUIET_MS));
        if (now - last_battery >= BATTERY_PERIOD_MS) {
            last_battery = now;
            update_battery();
        }
    }
}

void as_app_start(bool audio_ok, bool battery_ok) {
    s_battery_ok = battery_ok;
    as_sine_init();   // 正弦表由音频任务与界面共用，先在这里建好

    as_cfg_t cfg;
    as_store_init(&cfg);
    as_model_init(&s_model, &cfg, now_ms());
    as_power_init(&s_power, now_ms());
    as_lowbatt_init(&s_lowbatt);

    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(msg_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "创建事件队列失败");
        return;
    }
    if (!as_audio_start(audio_ok)) ESP_LOGE(TAG, "音频任务启动失败，无法出声");
    as_audio_set(&s_model.cfg, false, s_model.fade);

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败，界面无法建立");
    } else {
        const int missing = as_fonts_selfcheck();
        if (missing) ESP_LOGE(TAG, "有 %d 个字形缺失，界面可能出现方块", missing);
        as_ui_init(audio_ok);
        as_ui_set_battery(battery_ok ? bsp_battery_soc() : -1);
        as_ui_show(&s_model, now_ms());
        lv_timer_create(anim_cb, ANIM_PERIOD_MS, NULL);
        bsp_lvgl_unlock();
    }
    set_backlight(as_power_backlight(&s_power));

    if (xTaskCreate(app_task, "as_app", APP_TASK_STACK, NULL, APP_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "创建应用任务失败");
        return;
    }
    // 按键最后接入：队列、任务、界面都就绪之后才会有事件进来。
    const esp_err_t e = bsp_button_init(on_button, NULL);
    if (e != ESP_OK) ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(e));
    ESP_LOGI(TAG, "ASMR 声景播放器已启动（音频 %s，电量 %s）", audio_ok ? "可用" : "不可用",
             battery_ok ? "可用" : "不可用");
}
