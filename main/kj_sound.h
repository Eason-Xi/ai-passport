// main/kj_sound.h —— 提示音：合成的短音型，在独立任务里播放（按键回调 / 应用任务只入队）。
#pragma once

#include "esp_err.h"
#include "kj_flow.h"

// 需要先 bsp_audio_init() 成功；失败时 kj_sound_play() 静默忽略。
esp_err_t kj_sound_init(void);
void kj_sound_play(kj_cue_t cue);
