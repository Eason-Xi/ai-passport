// main/main.c —— 乐器节拍器固件入口：初始化 BSP，然后交给 mn_app。
//
// 显示与 LVGL 是硬依赖（失败就无法使用）；音频与电量计是软依赖，失败时应用降级运行：
// 没有音频时顶栏提示"音频不可用"、节拍仍以画面显示，没有电量计时隐藏电量。
// 启动直接进入节拍器，不经过 baseline 的硬件测试菜单。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "mn_app.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "乐器节拍器启动");
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG, "自动关机后由按键唤醒，重新开机");
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，检查 SPI 接线（MOSI=%d SCLK=%d CS=%d DC=%d BL=%d）",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    const esp_err_t audio = bsp_audio_init();
    if (audio != ESP_OK) ESP_LOGW(TAG, "音频初始化失败：%s", esp_err_to_name(audio));
    const esp_err_t battery = bsp_battery_init();
    if (battery != ESP_OK) ESP_LOGW(TAG, "电量计不可用：%s", esp_err_to_name(battery));

    mn_app_start(audio == ESP_OK, battery == ESP_OK);
    ESP_LOGI(TAG, "空闲堆 %u 字节，最大连续块 %u 字节", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
