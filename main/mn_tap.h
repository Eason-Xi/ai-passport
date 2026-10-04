// main/mn_tap.h —— 敲击测速（纯逻辑）：用户跟着音乐连续按键，由按键时间戳算出 BPM。
//
// 规则：
//   * 两次敲击间隔 < MN_TAP_MIN_US（200 ms，高于 250 BPM 的 240 ms 留余量）视为按键抖动，忽略；
//   * 间隔 > MN_TAP_RESET_US（2.5 s，长于 30 BPM 的 2 s）视为重新开始；
//   * 已有 ≥2 个间隔时，新间隔偏离均值超过 35%，认为换了速度：以上一次敲击为起点重新累计；
//   * BPM = 最近至多 8 个间隔的平均值换算，四舍五入并夹紧到 30–250。
// 时间戳来自按键回调里的 esp_timer_get_time()（µs），避免队列排队延迟影响精度。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MN_TAP_MIN_US 200000
#define MN_TAP_RESET_US 2500000
#define MN_TAP_HISTORY 8

typedef struct {
    int64_t last_us;                  // 上一次被接受的敲击时间
    uint32_t iv[MN_TAP_HISTORY];      // 环形缓冲：最近的间隔（µs）
    uint8_t n_iv;                     // 有效间隔数 0..MN_TAP_HISTORY
    uint8_t head;                     // 下一个写入位置
    uint8_t taps;                     // 当前序列的敲击次数（饱和于 255）
} mn_tap_t;

void mn_tap_reset(mn_tap_t *t);
// 记录一次敲击；被当作抖动忽略时返回 false。
bool mn_tap_add(mn_tap_t *t, int64_t now_us);
// 当前估计的 BPM；不足一个间隔时返回 0。
uint16_t mn_tap_bpm(const mn_tap_t *t);
uint8_t mn_tap_count(const mn_tap_t *t);
