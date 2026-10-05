// main/kj_bump.h —— 碰拳配对：两位选手面对面同时长按 OK，庄家按"按下时刻"把他们配成一对。纯 C。
//
// 输入是所有"已碰拳、尚未结算"的选手及其按下时刻（庄家按收到时间 − 请求里的 age_ms 还原）。
// 以最早的一位 a 为基准，等到 now ≥ t_a + KJ_BUMP_SETTLE_MS 再结算：
//   * 与 a 相差 ≤ KJ_BUMP_PAIR_MS 的只有 b，且 b 附近也只有 a        → 配对；
//   * 一个都没有                                               → 落单（没碰到对手）；
//   * 其余（多人同时碰拳）：在这一簇里找时间最接近的两人，若相差 ≤ KJ_BUMP_TIE_MS 且簇里
//     其他人离他们都远于 3 倍这个差值，就放行这一对，其余人判"人太多"；否则整簇判"人太多"。
// 处理完一簇后继续找下一位到期的最早者。主机测试见 tests/test_kj_bump.c。
#pragma once

#include <stdint.h>

#define KJ_BUMP_PAIR_MS    600    // 两人按下时刻相差不超过这么多算"碰到"
#define KJ_BUMP_SETTLE_MS  900    // 从最早一人按下起等这么久再结算（等同伴的请求到达）
#define KJ_BUMP_TIE_MS     150    // 多人同时碰拳时，最接近的两人相差不超过这么多才放行
#define KJ_BUMP_MAX_AGE_MS 1500   // 请求在路上（含重发）超过这么久就作废
#define KJ_BUMP_MAX        16     // 一次结算最多考虑的碰拳者

typedef enum {
    KJ_BUMP_WAIT = 0,   // 还没到结算时间
    KJ_BUMP_PAIR,       // 配对成功：peer 为对方下标，dt 为两人按下时刻之差（ms）
    KJ_BUMP_ALONE,      // 没碰到任何人
    KJ_BUMP_CROWD,      // 同时碰拳的人太多，分不清谁和谁
} kj_bump_verdict_t;

typedef struct {
    uint8_t idx;        // 选手下标（调用方定义，原样写回 peer）
    uint32_t t;         // 按下时刻（单调毫秒，回绕安全）
} kj_bump_cand_t;

typedef struct {
    uint8_t verdict;    // kj_bump_verdict_t
    uint8_t peer;       // PAIR 时对方的 idx
    uint16_t dt;        // PAIR 时两人按下时刻之差
} kj_bump_out_t;

// out 与 c 一一对应；n 超过 KJ_BUMP_MAX 的部分全部判 WAIT。
void kj_bump_resolve(const kj_bump_cand_t *c, int n, uint32_t now_ms, kj_bump_out_t *out);
