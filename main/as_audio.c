// main/as_audio.c —— 音频任务，说明见 as_audio.h。
#include "as_audio.h"

#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "as_mixer.h"

static const char *TAG = "as_audio";

#define TASK_STACK 4096
#define TASK_PRIORITY 6            // 高于应用任务（5）与 LVGL 任务（4），保证 PCM 供给不断流
#define BLOCK BSP_AUDIO_DMA_FRAME_NUM                                       // 240 样本 = 15 ms
#define QUEUE_FRAMES (BSP_AUDIO_DMA_DESC_NUM * BSP_AUDIO_DMA_FRAME_NUM)     // 输出队列容量
#define BLOCK_MS (BLOCK * 1000 / AS_SAMPLE_RATE)
#define STATS_PERIOD_US (30 * 1000000LL)   // 播放中每 30 s 记录一次渲染耗时

_Static_assert(BLOCK <= AS_MIXER_MAX_BLOCK, "DMA 帧块不能超过混音器单次渲染上限");
_Static_assert(BLOCK * 1000 % AS_SAMPLE_RATE == 0, "空跑模式按整数毫秒延时");

// 应用任务写、音频任务读的请求快照。
static portMUX_TYPE s_req_mux = portMUX_INITIALIZER_UNLOCKED;
static as_cfg_t s_req_cfg;
static bool s_req_playing;
static int32_t s_req_fade = AS_Q15;

static TaskHandle_t s_task;
static bool s_hw_ok;
static volatile bool s_halt;       // 应用请求永久停手（关机前）
static volatile bool s_halted;     // 音频任务已确认停手
// 以下两项由 s_req_mux 保护：int64 在 32 位 RISC-V 上不是原子读写。
static bool s_streaming;
static int64_t s_quiet_since_us;   // 最后一块 PCM 预计播完的时刻

static volatile uint8_t s_meters[4];
static as_mixer_t s_mixer;         // 只在音频任务里访问（启动时除外）
static int16_t s_block[BLOCK];

void as_audio_set(const as_cfg_t *cfg, bool playing, int32_t fade) {
    taskENTER_CRITICAL(&s_req_mux);
    s_req_cfg = *cfg;
    s_req_playing = playing;
    s_req_fade = fade;
    taskEXIT_CRITICAL(&s_req_mux);
    if (s_task) xTaskNotifyGive(s_task);
}

void as_audio_meters(uint8_t out[4]) {
    for (int i = 0; i < 4; i++) out[i] = s_meters[i];
}

bool as_audio_quiet(uint32_t quiet_ms) {
    taskENTER_CRITICAL(&s_req_mux);
    const bool streaming = s_streaming;
    const int64_t since = s_quiet_since_us;
    taskEXIT_CRITICAL(&s_req_mux);
    return !streaming && esp_timer_get_time() - since >= (int64_t)quiet_ms * 1000;
}

static void set_streaming(bool on, int64_t quiet_since_us) {
    taskENTER_CRITICAL(&s_req_mux);
    s_streaming = on;
    if (!on) s_quiet_since_us = quiet_since_us;
    taskEXIT_CRITICAL(&s_req_mux);
}

// 写一块 PCM；硬件不可用或写入失败时按块时长延时，保持节奏。
static void output_block(TickType_t *wake) {
    if (s_hw_ok) {
        const esp_err_t e = bsp_audio_write(s_block, sizeof s_block);
        if (e == ESP_OK) return;
        ESP_LOGW(TAG, "PCM 写入失败：%s", esp_err_to_name(e));
    }
    vTaskDelayUntil(wake, pdMS_TO_TICKS(BLOCK_MS));
}

static void publish_meters(void) {
    for (int i = 0; i < AS_LAYERS; i++) s_meters[i] = s_mixer.layers[i].meter;
    s_meters[3] = s_mixer.meter;
}

static void audio_task(void *arg) {
    (void)arg;
    int applied_volume = -1;
    bool streaming = false;   // s_streaming 的任务内副本（只有本任务修改）
    TickType_t wake = xTaskGetTickCount();
    // 渲染耗时统计：一块 PCM 的预算是 15 ms，日志里的占比即混音合成的 CPU 占用。
    int64_t stats_since = esp_timer_get_time(), render_sum = 0, render_max = 0;
    uint32_t render_n = 0;

    for (;;) {
        if (s_halt) {
            // 关机流程接管外设：不再触碰 codec / I2S，挂起等待深睡。
            s_halted = true;
            vTaskSuspend(NULL);
        }
        as_cfg_t req;
        bool playing;
        int32_t fade;
        taskENTER_CRITICAL(&s_req_mux);
        req = s_req_cfg;
        playing = s_req_playing;
        fade = s_req_fade;
        taskEXIT_CRITICAL(&s_req_mux);

        if (s_hw_ok && req.volume != applied_volume) {
            bsp_audio_set_volume(as_cfg_volume_percent(req.volume));
            applied_volume = req.volume;
        }
        for (int i = 0; i < AS_LAYERS; i++) as_mixer_set_layer(&s_mixer, i, req.sound[i], req.level[i]);
        as_mixer_set_playing(&s_mixer, playing);
        as_mixer_set_fade(&s_mixer, fade);

        if (as_mixer_silent(&s_mixer)) {
            if (streaming) {
                streaming = false;
                publish_meters();
                // 队列里还有约 90 ms 的数据要播完。
                set_streaming(false, esp_timer_get_time() + (int64_t)QUEUE_FRAMES * 1000000 / AS_SAMPLE_RATE);
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
            continue;
        }
        if (!streaming) {
            streaming = true;
            set_streaming(true, 0);
            wake = xTaskGetTickCount();
        }
        const int64_t t0 = esp_timer_get_time();
        as_mixer_render(&s_mixer, s_block, BLOCK);
        const int64_t spent = esp_timer_get_time() - t0;
        render_sum += spent;
        render_max = spent > render_max ? spent : render_max;
        render_n++;
        if (t0 - stats_since >= STATS_PERIOD_US) {
            ESP_LOGI(TAG, "混音渲染：平均 %lld us / 块，最长 %lld us（预算 %d us，平均占用 %lld%%）",
                     render_sum / render_n, render_max, BLOCK_MS * 1000, render_sum * 100 / render_n / (BLOCK_MS * 1000));
            stats_since = t0;
            render_sum = render_max = 0;
            render_n = 0;
        }
        publish_meters();
        output_block(&wake);
    }
}

bool as_audio_halt(uint32_t timeout_ms) {
    if (!s_task) return true;
    s_halt = true;
    xTaskNotifyGive(s_task);
    for (uint32_t waited = 0; !s_halted && waited < timeout_ms; waited += 10) vTaskDelay(pdMS_TO_TICKS(10));
    return s_halted;
}

bool as_audio_start(bool hw_ok) {
    s_hw_ok = hw_ok;
    as_cfg_default(&s_req_cfg);
    as_mixer_init(&s_mixer);
    set_streaming(false, esp_timer_get_time());
    if (hw_ok) {
        const esp_err_t e = bsp_audio_set_format(AS_SAMPLE_RATE, 16, 1);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "设置音频格式失败（%s），改为无声运行", esp_err_to_name(e));
            s_hw_ok = false;
        }
    }
    if (xTaskCreate(audio_task, "as_audio", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "创建音频任务失败");
        s_task = NULL;
        return false;
    }
    ESP_LOGI(TAG, "音频任务已启动（%s，%d Hz，块 %d 样本，输出队列 %d 样本）", s_hw_ok ? "扬声器" : "无声空跑",
             AS_SAMPLE_RATE, BLOCK, QUEUE_FRAMES);
    return true;
}
