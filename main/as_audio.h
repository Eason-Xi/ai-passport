// main/as_audio.h —— 音频任务：持续把混音器输出送进 I2S。
//
// 线程模型：
//   应用任务 ── as_audio_set()（spinlock 快照 + 任务通知）──> 音频任务（优先级 6，独占 codec / I2S）
//   音频任务 ── 各层电平（原子字节）──> LVGL 任务的动画
//
// 播放时每 15 ms 渲染并阻塞写入一块 240 个样本（写满 DMA 队列后 bsp_audio_write 阻塞，
// 自然形成节拍）。暂停淡出结束后不再写入，I2S DMA 自动输出静音（auto_clear），任务睡眠
// 等待下一次请求。codec 音量（I2C）也在本任务里设置，与 PCM 写入串行，不会并发访问。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "as_cfg.h"

// 创建音频任务。hw_ok 为假（codec 初始化失败）时仍创建：按 15 ms 节奏空跑混音器，
// 界面照常，只是没有声音。必须在 as_audio_set 之前调用一次。
bool as_audio_start(bool hw_ok);

// 下发最新设置与播放状态（拷贝快照，立即返回）。fade 为睡眠定时渐弱增益（Q15）。
void as_audio_set(const as_cfg_t *cfg, bool playing, int32_t fade);

// 读取最近一块的电平：三层 + 混音输出（0..255）。任何任务都可调用。
void as_audio_meters(uint8_t out[4]);

// 音频已静止至少 quiet_ms 毫秒（没有在写 PCM，且最后一块已播完）。
// 应用据此决定能否写 Flash：写 Flash 会暂停 Cache，播放中写入会让 I2S 断流爆音。
bool as_audio_quiet(uint32_t quiet_ms);

// 关机前调用（应用已停止播放且 as_audio_quiet 为真）：让音频任务永久停止访问
// codec / I2S。等待任务确认最多 timeout_ms，返回是否已确认。
bool as_audio_halt(uint32_t timeout_ms);
