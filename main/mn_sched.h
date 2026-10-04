// main/mn_sched.h —— 采样级节拍调度与单声部混音（纯逻辑，无 ESP-IDF / LVGL 依赖）。
//
// 节拍器的"时钟"不是定时器，而是已经送进 I2S 的样本数：音频任务每次渲染一块 PCM，
// 本模块告诉它这一块里哪些样本位置要响一声 click，并把预合成的 click 波形混进去。
// 这样节拍间隔精确到单个样本（16 kHz 下 62.5 µs），且长时间运行不会累积漂移。
//
// 间隔计算：一个 tick（细分音）的长度 = fs × 60 / (bpm × subdiv) 个样本，通常不是整数。
// 用"整数商 + 余数累加"（Bresenham）推进：第 k 个 tick 恰好落在 floor(k × fs × 60 / D)，
// 误差永远小于 1 个样本，且没有浮点。
//
// 参数生效时机（音乐上更自然、也便于测试）：
//   * BPM：立即生效——从上一个 tick 起按新间隔重新推算下一个 tick（若已过期则在当前块开头响）；
//   * 细分：在下一拍的拍头生效，保证同一拍内的细分音等距；
//   * 拍号（每小节拍数）：在下一拍拍头检查，若当前拍序号已超出新拍数则回到第 1 拍；
//   * 首拍重音开关：立即生效（影响之后发出的 tick 类型）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MN_SAMPLE_RATE 16000u   // 与 BSP 默认格式一致：16 kHz / 16 bit / 单声道
#define MN_BPM_MIN 30u
#define MN_BPM_MAX 250u
#define MN_BEATS_MIN 1u
#define MN_BEATS_MAX 12u
#define MN_SUBDIV_MAX 4u        // 1=不细分 2=八分 3=三连音 4=十六分

// tick 类型，决定播放哪一种 click（也是 mn_sound 的力度档位）。
typedef enum {
    MN_TICK_ACCENT = 0,  // 小节首拍重音
    MN_TICK_BEAT,        // 普通拍
    MN_TICK_SUB,         // 拍内细分音
    MN_TICK_KIND_COUNT,
} mn_tick_kind_t;

// 节拍参数（调度器只关心这四项；音色、音量由音频任务处理）。
typedef struct {
    uint16_t bpm;     // MN_BPM_MIN..MN_BPM_MAX，超出会被夹紧
    uint8_t beats;    // 每小节拍数 MN_BEATS_MIN..MN_BEATS_MAX
    uint8_t subdiv;   // 每拍细分数 1..MN_SUBDIV_MAX
    bool accent;      // 首拍重音（拍数为 1 时不区分重音）
} mn_meter_t;

// 一个 tick 在当前块里的位置与身份。
typedef struct {
    uint32_t offset;  // 块内样本偏移，0..n-1
    uint8_t kind;     // mn_tick_kind_t
    uint8_t beat;     // 小节内拍序号 0..beats-1
    uint8_t sub;      // 拍内细分序号 0..subdiv-1
    uint8_t subdiv;   // 该拍生效的细分数
    uint8_t beats;    // 发出该 tick 时的每小节拍数
    uint16_t bpm;     // 发出该 tick 时的 BPM
    uint32_t beat_no; // 自开始以来的拍计数（细分音沿用所属拍的计数），用于摆杆左右交替
} mn_tick_t;

// 调度器状态。所有字段都属于内部实现，调用方只通过下面的函数访问。
typedef struct {
    mn_meter_t cur;        // 当前设定（BPM / 拍数 / 重音立即读取）
    uint8_t sub_active;    // 本拍实际使用的细分数（拍头才从 cur.subdiv 同步）
    uint64_t pos;          // 下一块第一个样本的绝对位置
    uint64_t next_tick;    // 下一个 tick 的绝对样本位置
    uint64_t last_tick;    // 上一个已发出 tick 的绝对位置（has_last 为真时有效）
    bool has_last;
    uint32_t step_whole;   // 间隔整数部分（样本）
    uint32_t step_rem;     // 间隔余数分子
    uint32_t step_den;     // 间隔余数分母 = bpm × sub_active
    uint32_t err;          // 余数累加器，恒 < step_den
    uint8_t beat;          // 下一个 tick 的拍序号
    uint8_t sub;           // 下一个 tick 的细分序号
    uint32_t beat_no;      // 下一个拍头的拍计数
} mn_sched_t;

// 把任意参数夹紧到合法范围（越界 BPM、0 拍、5 细分等）。
mn_meter_t mn_meter_sanitize(mn_meter_t meter);

// 从头开始：第一个 tick（第 1 拍拍头）落在下一块的第 0 个样本。
// 绝对位置从 0 重新计数。
void mn_sched_start(mn_sched_t *s, const mn_meter_t *meter);

// 更新参数（可在运行中任意时刻调用，生效时机见文件头说明）。
void mn_sched_set(mn_sched_t *s, const mn_meter_t *meter);

// 推进 n 个样本，把这段时间内的 tick 按时间顺序写入 out（最多 max 个，多余的丢弃并计入
// 返回值之外——调用方应保证 max 足够：最快 250 BPM × 4 细分时 60 ms 才一个 tick）。
// 返回写入的 tick 数。
size_t mn_sched_advance(mn_sched_t *s, uint32_t n, mn_tick_t *out, size_t max);

// 第 k 个 tick 的理论位置（用于测试与校验）：floor(k × fs × 60 / (bpm × subdiv))。
uint64_t mn_sched_ideal_pos(uint64_t k, uint16_t bpm, uint8_t subdiv);

// ---------------------------------------------------------------------------
// 单声部混音：新 tick 打断上一个 click（click 长度短于最小 tick 间隔，实际不会截断）。
// ---------------------------------------------------------------------------

// 三种力度的 click 波形（同一音色）。指针所指内存由调用方持有，渲染期间必须有效。
typedef struct {
    const int16_t *pcm[MN_TICK_KIND_COUNT];
    uint32_t len[MN_TICK_KIND_COUNT];
} mn_bank_t;

typedef struct {
    const int16_t *pcm;   // 正在播放的 click；NULL 表示空闲
    uint32_t len;
    uint32_t idx;         // 下一个要输出的样本
} mn_voice_t;

void mn_voice_reset(mn_voice_t *v);
bool mn_voice_active(const mn_voice_t *v);

// 渲染一块：先推进调度器得到 tick，再把 click 按 tick 位置写入 out（其余为 0）。
// ticks/max/count 返回本块的 tick（供界面同步）；bank 为 NULL 或 mute 为真时只推进时钟、
// 输出静音。sched 为 NULL 时不产生新 tick，只把正在播放的 click 尾巴渲染完（停止时使用）。
void mn_render(mn_sched_t *sched, mn_voice_t *voice, const mn_bank_t *bank, bool mute,
               int16_t *out, uint32_t n, mn_tick_t *ticks, size_t max, size_t *count);
