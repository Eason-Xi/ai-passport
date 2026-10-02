// main/main.c —— 颜真卿字帖（多宝塔碑）固件入口：初始化 BSP，然后交给 yz_app。
//
// 显示与 LVGL 是硬依赖（失败就无法使用）；音频与电量计是软依赖，失败时应用降级运行：
// 没有音频时计时结束不发声，没有电量计时隐藏电量。
// baseline 的 demo_*.c / ui_pixel*.c 仍留在仓库里供主机测试使用，但不编进本固件。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"

#include "yz_app.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "颜真卿字帖（多宝塔碑）启动");

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，检查 SPI 接线（MOSI=%d SCLK=%d CS=%d DC=%d BL=%d）",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    // 界面建好之前先关背光，避免显示上电后的随机内容；yz_app 会按设置点亮。
    bsp_display_backlight(0);

    const esp_err_t audio = bsp_audio_init();
    if (audio != ESP_OK) ESP_LOGW(TAG, "音频初始化失败：%s", esp_err_to_name(audio));
    const esp_err_t battery = bsp_battery_init();
    if (battery != ESP_OK) ESP_LOGW(TAG, "电量计不可用：%s", esp_err_to_name(battery));

    yz_app_start(audio == ESP_OK, battery == ESP_OK);
}
