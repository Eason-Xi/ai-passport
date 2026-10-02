// main/yz_chime.c —— 提示音任务。PCM 写入会阻塞，只能在这里做，不能放进按键回调或 LVGL 任务。
#include "yz_chime.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "yz_bell.h"

static const char *TAG = "yz_chime";

#define TASK_STACK 3072
#define TASK_PRIORITY 6          // 高于应用任务（5）与 LVGL（4），保证 I2S 不断流
#define CHUNK 256
#define PEAK 12000
#define VOLUME 70
#define TAIL_SAMPLES 1600        // 末尾 100 ms 静音，把 DMA 里的余音推出去

static SemaphoreHandle_t s_request;
static volatile bool s_active;
static bool s_format_ok;
static yz_bell_t s_bell;
static int16_t s_pcm[CHUNK];

static void chime_task(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_request, portMAX_DELAY);
        s_active = true;
        if (!s_format_ok) {
            if (bsp_audio_set_format(YZ_BELL_RATE, 16, 1) == ESP_OK) {
                bsp_audio_set_volume(VOLUME);
                s_format_ok = true;
            } else {
                ESP_LOGW(TAG, "设置 16 kHz 单声道失败，本次不发声");
            }
        }
        if (s_format_ok) {
            yz_bell_start(&s_bell, PEAK);
            size_t n;
            while ((n = yz_bell_render(&s_bell, s_pcm, CHUNK)) > 0) {
                if (bsp_audio_write(s_pcm, n * sizeof(int16_t)) != ESP_OK) {
                    ESP_LOGW(TAG, "PCM 写入失败");
                    s_format_ok = false;   // 下次重新设置格式
                    break;
                }
            }
            for (int i = 0; i < CHUNK; i++) s_pcm[i] = 0;
            for (int left = TAIL_SAMPLES; s_format_ok && left > 0; left -= CHUNK) {
                bsp_audio_write(s_pcm, CHUNK * sizeof(int16_t));
            }
        }
        s_active = false;
    }
}

bool yz_chime_start(bool audio_ok) {
    if (!audio_ok) return false;
    s_request = xSemaphoreCreateBinary();
    if (!s_request) return false;
    if (xTaskCreate(chime_task, "yz_chime", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        vSemaphoreDelete(s_request);
        s_request = NULL;
        return false;
    }
    return true;
}

void yz_chime_play(void) {
    if (s_request && !s_active) xSemaphoreGive(s_request);
}

bool yz_chime_active(void) {
    return s_active;
}
