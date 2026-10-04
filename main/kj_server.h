// main/kj_server.h —— 庄家（主机）侧的协议逻辑：把收到的帧交给规则引擎，
// 决定何时广播信标、何时给哪位选手推送 / 重发视图，并驱动电脑选手。
// 纯 C：平台层（main/kj_host_app.c）只负责收发字节与串口。主机测试见 tests/test_kj_server.c。
#pragma once

#include "kj_proto.h"
#include "kj_rules.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_ROOM_BEACON_MS     1000   // 信标周期
#define KJ_ROOM_BEACON_MIN_MS  150   // 状态变化时的最短广播间隔
#define KJ_VIEW_RETRY_MS       400   // 视图未确认时的重发间隔
#define KJ_VIEW_RETRY_MAX        6   // 连续重发上限（之后等心跳里的版本号再补发）
#define KJ_VIEW_SENDS_PER_TICK   8   // 每次 tick 最多推送的视图数（防止人多时突发拥塞）

typedef struct {
    uint16_t sent_ver;
    uint16_t acked_ver;
    uint32_t sent_ms;
    uint8_t tries;
    uint8_t force;       // 收到过期心跳 / 重复请求时立即补发
} kj_link_t;

typedef enum {
    KJ_CMD_NONE = 0,
    KJ_CMD_START,
    KJ_CMD_END,
    KJ_CMD_NEW_GAME,
    KJ_CMD_RESET,
    KJ_CMD_BOT_ADD,
    KJ_CMD_BOT_REMOVE,
    KJ_CMD_KICK,        // arg = 选手编号
    KJ_CMD_SYNC,        // 看板请求全量刷新（不改状态）
} kj_cmd_t;

typedef struct {
    kj_game_t game;
    uint16_t room;
    uint32_t last_beacon_ms;
    uint16_t beacon_phase_ver;
    bool beacon_sent;
    uint32_t rng;
    uint32_t rx_frames, rx_bad, tx_views, tx_beacons;
    kj_link_t link[KJ_MAX_PLAYERS];
    uint8_t view_cursor;  // 轮询起点，保证人多时也公平
} kj_server_t;

void kj_server_init(kj_server_t *s, uint16_t room, uint32_t seed);
// 收到一帧（任意来源）。需要立即回复的帧放进 out。
void kj_server_on_frame(kj_server_t *s, const uint8_t mac[6], int8_t rssi,
                        const uint8_t *data, size_t len, uint32_t now_ms, kj_outbox_t *out);
// 周期调用（建议 ≤ 50 ms）：超时、电脑选手、信标、视图推送。
void kj_server_tick(kj_server_t *s, uint32_t now_ms, kj_outbox_t *out);
// 庄家命令（看板串口 / 主机菜单）。返回 KJ_N_NONE 表示成功。
kj_notice_t kj_server_command(kj_server_t *s, kj_cmd_t cmd, int arg, uint32_t now_ms);
// 生成当前信标内容（主机界面与测试用）。
void kj_server_room_info(const kj_server_t *s, uint32_t now_ms, kj_room_t *out);

// 电脑选手的决策延迟（毫秒），按对决编号与座位做确定性抖动，便于测试复现。
uint32_t kj_bot_accept_delay(uint16_t salt, int idx);
uint32_t kj_bot_play_delay(uint16_t duel_id, int idx);
// 空闲多久后主动找另一位空闲的电脑选手对决。
uint32_t kj_bot_idle_delay(uint32_t salt, int idx);
// 电脑选手选牌：偏向剩余最多的牌，带随机性。rng 为 xorshift32 状态。
uint8_t kj_bot_choose(const kj_player_t *p, uint32_t *rng);
