// main/mn_audio.h —— 节拍器音频任务：持续向 I2S 送 PCM，以已写样本数作节拍时钟。
//
// 线程模型：
//   应用任务 ── mn_audio_set()（spinlock 快照 + 任务通知）──> 音频任务（优先级 6，独占 I2S）
//   音频任务 ── 节拍事件环形缓冲（spinlock）──> LVGL 任务在发声时刻取出（mn_audio_pop）
//
// 计时原理：运行时每 15 ms（一个 DMA 帧块 = 240 样本）渲染并阻塞写入一块；开始时先写满
// 一整个 DMA 队列的静音，使队列此后始终处于"满"的稳态。写入返回时，最新样本大约在
// 队列容量（6 × 240 = 1440 样本 = 90 ms）之后才发声，据此给每个 tick 算出发声时刻，
// 界面在那一刻再点亮节拍点，实现声画同步。
// 停止时把正在响的 click 尾巴写完就不再写入，I2S DMA 自动输出静音（auto_clear）。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mn_beat.h"
#include "mn_cfg.h"

// 预合成全部音色（约 11.5 KB 静态内存）并创建音频任务。hw_ok 为假（codec 初始化失败）时
// 仍创建任务：按 15 ms 节奏空跑时钟，界面节拍照常，只是没有声音。
// 必须在应用任务调用 mn_audio_set 之前调用一次。
bool mn_audio_start(bool hw_ok);

// 下发最新设置与运行状态（拷贝快照，立即返回，可在应用任务中随时调用）。
// BPM / 拍号 / 细分 / 重音按 mn_sched 的规则生效；音色与音量在下一块生效。
// countin 只在"停止 → 运行"的那一次起作用：为真时先发出 3 个每秒一次的倒数音，
// 第 3 秒整响起第 1 拍（节拍事件里 kind = MN_TICK_COUNT，beat = 剩余秒数）。
void mn_audio_set(const mn_cfg_t *cfg, bool running, bool countin);

// LVGL 上下文调用：取出一个发声时刻已到（play_us <= now_us）的节拍事件。没有则返回 false。
bool mn_audio_pop(int64_t now_us, mn_beat_t *out);

// 音频已静止至少 quiet_ms 毫秒（未在播放，且最后一块 PCM 已播完）。
// 应用据此决定能否写 Flash：写 Flash 会暂停 Cache，播放中写入会让 I2S 断流爆音。
bool mn_audio_quiet(uint32_t quiet_ms);

// 关机前调用（应用已停止播放且 mn_audio_quiet 为真）：让音频任务永久停止访问 codec / I2S，
// 之后才能安全执行 bsp_audio_sleep() 等终端关闭步骤。等待任务确认最多 timeout_ms，
// 返回是否已确认。调用后本次运行内不能再恢复音频。
bool mn_audio_halt(uint32_t timeout_ms);
