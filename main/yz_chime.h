// main/yz_chime.h —— 提示音任务：在独立任务里合成并播放“磬”声，调用方只投递请求。
#pragma once

#include <stdbool.h>

// 启动音频任务。audio_ok 为 false 时任务不启动，yz_chime_play() 静默忽略。
bool yz_chime_start(bool audio_ok);
// 非阻塞：请求播放一次（正在播放时忽略）。
void yz_chime_play(void);
// 是否正在播放（用于推迟 NVS 写入）。
bool yz_chime_active(void);
