// main/kj_client.h —— 选手设备侧的协议逻辑：找赌局、入座、可靠请求、视图同步、可挑战的对手名单。
// 纯 C：时间与收发都由调用方注入，主机测试见 tests/test_kj_client.c、tests/test_kj_sim.c 与
// tests/test_kj_sim_hub.c。帧经电脑 hub 中继（UDP），心跳只单播给庄家。
#pragma once

#include "kj_proto.h"
#include "kj_rules.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_CLIENT_MAX_ROOMS      6
#define KJ_CLIENT_ROOM_TTL_MS    4500   // 多久没听到信标就从赌局列表移除
#define KJ_CLIENT_HOST_TTL_MS    4500   // 多久没收到庄家任何帧视为失联
#define KJ_CLIENT_HELLO_MS       1500
#define KJ_CLIENT_REQ_RETRY_MS    250
#define KJ_CLIENT_REQ_TRIES        12   // 约 3 s 没有确认就放弃并提示

typedef struct {
    uint16_t room;
    uint8_t mac[6];
    uint8_t phase;
    uint8_t seated;
    uint32_t seen_ms;
} kj_room_entry_t;

typedef struct {
    uint8_t no;
    uint8_t is_bot;
} kj_opponent_t;

typedef enum {
    KJ_LINK_IDLE = 0,    // 未选择赌局
    KJ_LINK_JOINING,     // 已发出入座请求
    KJ_LINK_JOINED,      // 已入座（收到 no != 0 的视图）
} kj_link_state_t;

typedef struct {
    kj_room_entry_t rooms[KJ_CLIENT_MAX_ROOMS];

    uint8_t link;              // kj_link_state_t
    uint16_t room;
    uint8_t host_mac[6];
    kj_room_t room_info;
    bool have_room_info;
    uint32_t last_host_rx_ms;

    kj_view_t view;
    bool have_view;
    uint32_t view_rx_ms;
    bool view_changed;         // 供 UI 消费：自上次 take 后视图有更新
    uint8_t kicked_notice;     // 被庄家移出（或入座被拒）的原因，UI 消费后清零
    bool kicked;

    uint16_t seq;
    uint16_t boot;             // 本次开机的随机数（非 0），随每个请求发给庄家
    bool pending;
    kj_req_t req;
    uint32_t req_t0_ms;        // 当前请求的发起时刻（每次发送据此填写 age_ms）
    uint32_t req_next_ms;
    uint8_t req_tries;
    bool req_failed;           // 请求多次无确认，UI 消费后清零

    uint32_t next_hello_ms;
    bool ack_due;
    uint32_t rng;
} kj_client_t;

void kj_client_init(kj_client_t *c, uint32_t seed);
void kj_client_on_frame(kj_client_t *c, const uint8_t mac[6], const uint8_t *data, size_t len, uint32_t now_ms);
void kj_client_tick(kj_client_t *c, uint32_t now_ms, kj_outbox_t *out);

// 列出当前听得到的赌局（按赌局号排序，列表不会来回跳），返回个数。
int kj_client_rooms(const kj_client_t *c, uint32_t now_ms, kj_room_entry_t *out, int max);
// 向某个赌局发起入座（room 为赌局号）。找不到该赌局返回 false。
bool kj_client_join(kj_client_t *c, uint16_t room, uint32_t now_ms);
// 本地离开（回到找赌局状态；庄家那边的座位保留，重新入座即恢复）。
void kj_client_leave(kj_client_t *c);
// 发起请求；未入座或已有未确认请求时返回 false。
bool kj_client_request(kj_client_t *c, uint8_t op, uint8_t arg, uint32_t now_ms);
bool kj_client_connected(const kj_client_t *c, uint32_t now_ms);
// 可挑战的对手：真人在前、电脑选手在后，各自按编号；不含自己。
int kj_client_opponents(const kj_client_t *c, kj_opponent_t *out, int max);
// UI 消费标志
bool kj_client_take_view_changed(kj_client_t *c);
bool kj_client_take_req_failed(kj_client_t *c);
bool kj_client_take_kicked(kj_client_t *c, uint8_t *notice);
