// main/mn_audio.c —— 节拍器音频任务，说明见 mn_audio.h。
#include "mn_audio.h"

#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "mn_sched.h"
#include "mn_sound.h"

static const char *TAG = "mn_audio";

#define TASK_STACK 4096
#define TASK_PRIORITY 6            // 高于应用任务（5）与 LVGL 任务（4），保证 PCM 供给不断流
#define BLOCK BSP_AUDIO_DMA_FRAME_NUM                        // 每次写入 240 样本 = 15 ms
#define QUEUE_FRAMES (BSP_AUDIO_DMA_DESC_NUM * BSP_AUDIO_DMA_FRAME_NUM)   // 输出队列容量
#define BLOCK_MS (BLOCK * 1000 / MN_SAMPLE_RATE)
#define RING_SIZE 16
#define MAX_TICKS 8

_Static_assert(BLOCK * 1000 % MN_SAMPLE_RATE == 0, "一块必须是整数毫秒，空跑模式按 tick 节拍延时");

// 应用任务写、音频任务读的请求快照。
static portMUX_TYPE s_req_mux = portMUX_INITIALIZER_UNLOCKED;
static mn_cfg_t s_req_cfg;
static bool s_req_running;

// 音频任务写、LVGL 任务读的节拍事件环形缓冲。
static portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;
static mn_beat_t s_ring[RING_SIZE];
static uint8_t s_ring_head;        // 最早的事件
static uint8_t s_ring_count;

static TaskHandle_t s_task;
static bool s_hw_ok;
// 以下两项由 s_req_mux 保护：int64 在 32 位 RISC-V 上不是原子读写。
static bool s_streaming;           // 正在写 PCM
static int64_t s_quiet_since_us;   // 最后一块 PCM 预计播完的时刻

// 全部音色 × 力度的 click（启动时合成，只读）。
static int16_t s_clicks[MN_SOUND_COUNT][MN_TICK_KIND_COUNT][MN_CLICK_LEN];
static mn_bank_t s_banks[MN_SOUND_COUNT];
static int16_t s_block[BLOCK];

static void ring_clear(void) {
    taskENTER_CRITICAL(&s_ring_mux);
    s_ring_head = 0;
    s_ring_count = 0;
    taskEXIT_CRITICAL(&s_ring_mux);
}

static void ring_push(const mn_beat_t *b) {
    taskENTER_CRITICAL(&s_ring_mux);
    if (s_ring_count == RING_SIZE) {
        // 界面来不及取（不应发生）：丢最早的一个，保证最新节拍不丢。
        s_ring_head = (uint8_t)((s_ring_head + 1) % RING_SIZE);
        s_ring_count--;
    }
    s_ring[(s_ring_head + s_ring_count) % RING_SIZE] = *b;
    s_ring_count++;
    taskEXIT_CRITICAL(&s_ring_mux);
}

bool mn_audio_pop(int64_t now_us, mn_beat_t *out) {
    bool got = false;
    taskENTER_CRITICAL(&s_ring_mux);
    if (s_ring_count && s_ring[s_ring_head].play_us <= now_us) {
        *out = s_ring[s_ring_head];
        s_ring_head = (uint8_t)((s_ring_head + 1) % RING_SIZE);
        s_ring_count--;
        got = true;
    }
    taskEXIT_CRITICAL(&s_ring_mux);
    return got;
}

void mn_audio_set(const mn_cfg_t *cfg, bool running) {
    taskENTER_CRITICAL(&s_req_mux);
    s_req_cfg = *cfg;
    s_req_running = running;
    taskEXIT_CRITICAL(&s_req_mux);
    if (s_task) xTaskNotifyGive(s_task);
}

bool mn_audio_quiet(uint32_t quiet_ms) {
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

// 写一块 PCM；硬件不可用或写入失败时按块时长延时，保持时钟节奏。返回是否真正写入了 I2S。
static bool output_block(const int16_t *pcm, TickType_t *wake) {
    if (s_hw_ok) {
        const esp_err_t e = bsp_audio_write(pcm, sizeof(int16_t) * BLOCK);
        if (e == ESP_OK) return true;
        ESP_LOGW(TAG, "PCM 写入失败：%s", esp_err_to_name(e));
    }
    vTaskDelayUntil(wake, pdMS_TO_TICKS(BLOCK_MS));
    return false;
}

static void audio_task(void *arg) {
    (void)arg;
    mn_sched_t sched;
    mn_voice_t voice;
    mn_cfg_t cur;
    mn_cfg_default(&cur);
    mn_voice_reset(&voice);
    mn_tick_t ticks[MAX_TICKS];
    bool running = false;
    bool streaming_local = false;  // s_streaming 的任务内副本（只有本任务修改）
    int applied_volume = -1;
    uint64_t written = 0;         // 本次运行已写入（或空跑）的样本数
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        mn_cfg_t req;
        bool run_req;
        taskENTER_CRITICAL(&s_req_mux);
        req = s_req_cfg;
        run_req = s_req_running;
        taskEXIT_CRITICAL(&s_req_mux);

        if (s_hw_ok && req.volume != applied_volume) {
            // codec 音量走 I2C，与 I2S 写入在同一任务里串行执行。
            bsp_audio_set_volume(mn_cfg_volume_percent(req.volume));
            applied_volume = req.volume;
        }

        if (run_req && !running) {
            // 开始：清掉旧事件，先写满一整个输出队列的静音，此后队列始终是满的。
            ring_clear();
            cur = req;
            const mn_meter_t meter = mn_cfg_meter(&cur);
            mn_sched_start(&sched, &meter);
            mn_voice_reset(&voice);
            running = true;
            set_streaming(true, 0);
            streaming_local = true;
            written = 0;
            wake = xTaskGetTickCount();
            memset(s_block, 0, sizeof s_block);
            for (int i = 0; i < BSP_AUDIO_DMA_DESC_NUM; i++) {
                output_block(s_block, &wake);
                written += BLOCK;
            }
        } else if (!run_req && running) {
            running = false;   // 不再产生新 tick，下面继续把 click 尾巴渲染完
        }

        if (running) {
            const mn_meter_t old = mn_cfg_meter(&cur), next = mn_cfg_meter(&req);
            if (memcmp(&old, &next, sizeof old) != 0) mn_sched_set(&sched, &next);
            cur = req;
        }

        if (!running && !mn_voice_active(&voice)) {
            if (streaming_local) {
                streaming_local = false;
                // 队列里还有约 90 ms 的数据要播完。
                set_streaming(false, esp_timer_get_time() + (int64_t)QUEUE_FRAMES * 1000000 / MN_SAMPLE_RATE);
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
            continue;
        }

        size_t n = 0;
        mn_render(running ? &sched : NULL, &voice, &s_banks[cur.sound < MN_SOUND_COUNT ? cur.sound : 0],
                  cur.volume == 0, s_block, BLOCK, ticks, MAX_TICKS, &n);
        const bool real = output_block(s_block, &wake);
        const int64_t t_after = esp_timer_get_time();
        // 本块之后已写入 written + BLOCK 个样本；真实输出时队列是满的，
        // 当前正在播放的大约是第 (written + BLOCK - QUEUE_FRAMES) 个样本。
        const int64_t playing = real ? (int64_t)(written + BLOCK) - QUEUE_FRAMES : (int64_t)written;
        for (size_t i = 0; i < n; i++) {
            const int64_t sample = (int64_t)written + ticks[i].offset;
            const mn_beat_t b = {
                .play_us = t_after + (sample - playing) * 1000000 / MN_SAMPLE_RATE,
                .beat_no = ticks[i].beat_no,
                .bpm = ticks[i].bpm,
                .beat = ticks[i].beat,
                .sub = ticks[i].sub,
                .subdiv = ticks[i].subdiv,
                .beats = ticks[i].beats,
                .kind = ticks[i].kind,
            };
            ring_push(&b);
        }
        written += BLOCK;
    }
}

bool mn_audio_start(bool hw_ok) {
    s_hw_ok = hw_ok;
    mn_cfg_default(&s_req_cfg);
    set_streaming(false, esp_timer_get_time());
    for (int s = 0; s < MN_SOUND_COUNT; s++) {
        for (int k = 0; k < MN_TICK_KIND_COUNT; k++) {
            mn_sound_render((uint8_t)s, (uint8_t)k, s_clicks[s][k]);
            s_banks[s].pcm[k] = s_clicks[s][k];
            s_banks[s].len[k] = MN_CLICK_LEN;
        }
    }
    if (hw_ok) {
        const esp_err_t e = bsp_audio_set_format(MN_SAMPLE_RATE, 16, 1);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "设置音频格式失败（%s），改为无声运行", esp_err_to_name(e));
            s_hw_ok = false;
        }
    }
    if (xTaskCreate(audio_task, "mn_audio", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "创建音频任务失败");
        s_task = NULL;
        return false;
    }
    ESP_LOGI(TAG, "音频任务已启动（%s，块 %d 样本，输出队列 %d 样本）", s_hw_ok ? "扬声器" : "无声空跑",
             BLOCK, QUEUE_FRAMES);
    return true;
}
