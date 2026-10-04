// main/mn_tempo.h —— 速度术语与 BPM 调节规则（纯逻辑）。
#pragma once

#include <stdint.h>

// 速度术语序号（文字在 mn_strings.h 的 MN_TEMPO_IT / MN_TEMPO_ZH 中，下标与此一致）。
typedef enum {
    MN_TEMPO_GRAVE = 0,     // < 40      庄板
    MN_TEMPO_LARGO,         // 40–59     广板
    MN_TEMPO_LARGHETTO,     // 60–65     小广板
    MN_TEMPO_ADAGIO,        // 66–75     柔板
    MN_TEMPO_ANDANTE,       // 76–107    行板
    MN_TEMPO_MODERATO,      // 108–119   中板
    MN_TEMPO_ALLEGRO,       // 120–155   快板
    MN_TEMPO_VIVACE,        // 156–175   活板
    MN_TEMPO_PRESTO,        // 176–199   急板
    MN_TEMPO_PRESTISSIMO,   // ≥ 200     最急板
    MN_TEMPO_COUNT,
} mn_tempo_term_t;

uint8_t mn_tempo_term(uint16_t bpm);

// 夹紧到 30–250。
uint16_t mn_bpm_clamp(int bpm);

// BPM 调整一步：step 为 1 时 ±1；step 为 5 时跳到该方向上下一个 5 的整数倍（如 103 → 105 / 100）。
uint16_t mn_bpm_bump(uint16_t bpm, int dir, uint8_t step);

// 长按连调的节奏：第 n 次自动步进（n 从 0 开始，LONG 事件时执行第 0 次）之后，
// 距下一次步进的等待时间与下一次的步长。
//   n < 8：每 120 ms 一步，步长 1；n < 24：每 50 ms 一步，步长 1；之后每 90 ms 跳到下一个 5 的倍数。
uint32_t mn_repeat_delay_ms(uint32_t n);
uint8_t mn_repeat_step(uint32_t n);
