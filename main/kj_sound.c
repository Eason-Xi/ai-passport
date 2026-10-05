// main/kj_sound.c —— 提示音合成与播放（16 kHz 16 位单声道，不存音频文件）。
#include "kj_sound.h"

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <math.h>
#include <stdint.h>

static const char *TAG = "kj_sound";

#define RATE 16000
#define CHUNK 240
#define VOLUME_PERCENT 70

typedef struct {
    uint16_t hz;    // 0 = 静音
    uint16_t ms;
} note_t;

#define END { 0, 0 }

static const note_t CUE_KEY[] = { { 1800, 18 }, END };
static const note_t CUE_ALERT[] = { { 988, 90 }, { 0, 40 }, { 1319, 90 }, { 0, 40 }, { 988, 90 }, { 0, 40 },
                                    { 1319, 140 }, END };
static const note_t CUE_DUEL[] = { { 659, 80 }, { 880, 120 }, END };
static const note_t CUE_LOCK[] = { { 440, 40 }, { 330, 60 }, END };
static const note_t CUE_WIN[] = { { 784, 90 }, { 988, 90 }, { 1175, 90 }, { 1568, 220 }, END };
static const note_t CUE_LOSE[] = { { 392, 140 }, { 330, 140 }, { 262, 260 }, END };
static const note_t CUE_DRAW[] = { { 587, 110 }, { 0, 50 }, { 587, 110 }, END };
static const note_t CUE_CLEARED[] = { { 523, 100 }, { 659, 100 }, { 784, 100 }, { 1047, 160 }, { 0, 60 },
                                      { 1047, 120 }, { 1319, 320 }, END };
static const note_t CUE_OUT[] = { { 330, 200 }, { 311, 200 }, { 294, 200 }, { 220, 420 }, END };
static const note_t CUE_NOTICE[] = { { 1047, 60 }, { 0, 30 }, { 784, 80 }, END };
static const note_t CUE_ERROR[] = { { 220, 120 }, { 0, 40 }, { 220, 120 }, END };
static const note_t CUE_BUMP[] = { { 196, 50 }, { 0, 20 }, { 392, 70 }, END };
static const note_t CUE_MATCH[] = { { 880, 70 }, { 1175, 70 }, { 1568, 140 }, END };

static const note_t *const CUES[KJ_CUE_COUNT] = {
    [KJ_CUE_KEY] = CUE_KEY, [KJ_CUE_ALERT] = CUE_ALERT, [KJ_CUE_DUEL] = CUE_DUEL, [KJ_CUE_LOCK] = CUE_LOCK,
    [KJ_CUE_WIN] = CUE_WIN, [KJ_CUE_LOSE] = CUE_LOSE, [KJ_CUE_DRAW] = CUE_DRAW,
    [KJ_CUE_CLEARED] = CUE_CLEARED, [KJ_CUE_OUT] = CUE_OUT, [KJ_CUE_NOTICE] = CUE_NOTICE,
    [KJ_CUE_ERROR] = CUE_ERROR, [KJ_CUE_BUMP] = CUE_BUMP, [KJ_CUE_MATCH] = CUE_MATCH,
};

static QueueHandle_t s_queue;
static int16_t s_buf[CHUNK];

// 基波 + 三次谐波（比纯正弦更"游戏机"），整数相位累加查表合成（C3 没有浮点单元），
// 前后 4 ms 线性淡入淡出避免爆音。
static int16_t s_sine[256];

static void build_sine(void)
{
    for (int i = 0; i < 256; i++) s_sine[i] = (int16_t)(sinf(6.2831853f * (float)i / 256.0f) * 32767.0f);
}

static void play_note(const note_t *n)
{
    int total = RATE * n->ms / 1000;
    int fade = RATE * 4 / 1000;
    uint32_t phase = 0;
    uint32_t step = n->hz ? (uint32_t)(((uint64_t)n->hz << 32) / RATE) : 0;
    for (int done = 0; done < total;) {
        int len = total - done < CHUNK ? total - done : CHUNK;
        for (int i = 0; i < len; i++) {
            int k = done + i;
            int env = 256;
            if (k < fade) env = k * 256 / fade;
            if (total - k < fade) env = (total - k) * 256 / fade;
            int32_t v = 0;
            if (n->hz) {
                uint8_t idx = (uint8_t)(phase >> 24);
                int32_t s1 = s_sine[idx];
                int32_t s3 = s_sine[(uint8_t)(idx * 3)];
                v = (s1 * 3 + s3) / 4;            // 0.75 基波 + 0.25 三次谐波
                phase += step;
            }
            // 峰值约 0.27 满幅：外放够响又不削波
            s_buf[i] = (int16_t)((v * env / 256) * 9 / 32);
        }
        if (bsp_audio_write(s_buf, (size_t)len * sizeof(int16_t)) != ESP_OK) return;
        done += len;
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    uint8_t cue;
    for (;;) {
        if (xQueueReceive(s_queue, &cue, portMAX_DELAY) != pdTRUE) continue;
        // 积压时只播最新的一个，避免提示音拖在界面后面；按键轻点不会盖掉更重要的提示音。
        uint8_t newer;
        while (xQueueReceive(s_queue, &newer, 0) == pdTRUE) {
            if (newer != KJ_CUE_KEY || cue == KJ_CUE_KEY) cue = newer;
        }
        if (cue == KJ_CUE_NONE || cue >= KJ_CUE_COUNT || !CUES[cue]) continue;
        for (const note_t *n = CUES[cue]; n->ms; n++) play_note(n);
        note_t tail = { 0, 30 };
        play_note(&tail);
    }
}

esp_err_t kj_sound_init(void)
{
    build_sine();
    esp_err_t err = bsp_audio_set_format(RATE, 16, 1);
    if (err != ESP_OK) return err;
    bsp_audio_set_volume(VOLUME_PERCENT);
    s_queue = xQueueCreate(4, sizeof(uint8_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(sound_task, "kj_sound", 3072, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "sound ready");
    return ESP_OK;
}

void kj_sound_play(kj_cue_t cue)
{
    if (!s_queue || cue == KJ_CUE_NONE) return;
    uint8_t c = (uint8_t)cue;
    (void)xQueueSend(s_queue, &c, 0);
}
