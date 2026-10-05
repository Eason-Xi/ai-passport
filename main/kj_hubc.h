// main/kj_hubc.h —— 设备侧的 hub 客户端：找电脑 hub、保活、昵称同步与查询、扫码登记。纯 C，
// 时间与收发由平台层注入（只产出要发的控制消息），主机测试见 tests/test_kj_hubc.c。
//
//   * 找不到 hub 时每秒发一次 DISCOVER：有上次的 hub 地址就和广播交替发；找到后每 5 s 单播保活，
//     12 s 没收到 hub 的任何数据报就算失联，回到寻找。找到之后只认这台 hub（同一网络里有两台也不会来回切）。
//   * 昵称以 hub 的登记表为准：OFFER 里带"对你有效"的昵称就照此更新本机（平台层写 NVS）；
//     hub 没有你的记录时，DISCOVER 里带上本机保存的昵称，由 hub 补回登记表。
//   * 别人的昵称按（赌局号、编号）缓存，界面需要时才查询（每次最多 6 个），roster_rev 变了整批作废。
//   * 登记：界面显示二维码期间每秒发一次 REG(token)，hub 回 REG_STATE（等扫码 / 已打开 / 完成）。
#pragma once

#include "kj_hubproto.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_HUBC_DISCOVER_MS   1000
#define KJ_HUBC_KEEPALIVE_MS  5000
#define KJ_HUBC_LOST_MS      12000
#define KJ_HUBC_REG_MS        1000
#define KJ_HUBC_NAME_RETRY_MS 1000
#define KJ_HUBC_NAME_CACHE      24
#define KJ_HUBC_OUT_MAX          4

typedef enum {
    KJ_HUB_SEARCHING = 0,   // 还没找到（或失联后重新寻找）
    KJ_HUB_OK,
} kj_hub_state_t;

typedef struct {
    uint8_t kind;           // KH_K_*
    uint32_t ip;            // 目标 IPv4（网络字节序无关：平台层自己约定）；0 = 广播
    uint16_t room;
    uint16_t len;
    uint8_t data[64];
} kj_hubc_out_t;

typedef struct {
    uint16_t room;
    uint8_t no;
    uint8_t flags;          // KH_NAME_*
    bool valid;
    uint32_t used_ms;
    char name[KJ_NAME_MAX + 1];
} kj_name_slot_t;

typedef struct {
    // 本机
    uint8_t mac[6];
    uint16_t boot;
    uint8_t role;           // kh_role_t
    uint16_t room;
    char fw[KH_FW_LEN + 1];
    char my_name[KJ_NAME_MAX + 1];
    uint32_t my_rev;
    bool my_changed;        // 昵称被 hub 更新过，平台层据此写 NVS
    // hub
    uint8_t state;          // kj_hub_state_t
    uint32_t hub_ip, hint_ip;
    uint32_t hub_id;
    uint16_t http_port, tcp_port;
    uint32_t roster_rev;
    bool incompatible;
    uint32_t last_rx_ms;
    uint32_t next_discover_ms;
    bool alt;               // 寻找时单播 / 广播交替
    bool discover_now;      // 角色变化等：尽快发一次
    // 昵称缓存与待查询
    kj_name_slot_t names[KJ_HUBC_NAME_CACHE];
    uint8_t want[KH_NAMES_MAX];
    int want_n;
    uint16_t want_room;
    uint32_t want_sent_ms;
    bool want_sent;
    // 登记
    bool reg_active;
    char token[KJ_REG_TOKEN_LEN + 1];
    uint8_t reg_state;      // kh_reg_state_t（本地起始为 WAITING）
    uint32_t next_reg_ms;
} kj_hubc_t;

// hint_ip：上次找到的 hub 地址（NVS），0 = 没有。
void kj_hubc_init(kj_hubc_t *h, const uint8_t mac[6], uint16_t boot, const char *fw, const char *my_name,
                  uint32_t my_rev, uint32_t hint_ip);
void kj_hubc_set_role(kj_hubc_t *h, uint8_t role, uint16_t room);
// 收到 hub 的任何数据报（包括中继的游戏帧）都调用，用于失联判定。
void kj_hubc_note_rx(kj_hubc_t *h, uint32_t now_ms);
// 处理 hub 发来的控制消息（OFFER / REG_STATE / NAMES）。
void kj_hubc_on_msg(kj_hubc_t *h, const kh_env_t *e, uint32_t src_ip, uint32_t now_ms);
// 周期调用：返回要发出的控制消息个数（写入 out）。
int kj_hubc_tick(kj_hubc_t *h, uint32_t now_ms, kj_hubc_out_t *out, int max);
kj_hub_state_t kj_hubc_state(const kj_hubc_t *h, uint32_t now_ms);

// 查某个座位的昵称。已缓存返回 true（*name 可能是空串 = 没登记 / 电脑选手，看 *flags）；
// 否则排队查询并返回 false。
bool kj_hubc_name(kj_hubc_t *h, uint16_t room, uint8_t no, uint32_t now_ms, const char **name, uint8_t *flags);

void kj_hubc_reg_start(kj_hubc_t *h, const char token[KJ_REG_TOKEN_LEN], uint32_t now_ms);
void kj_hubc_reg_stop(kj_hubc_t *h);
bool kj_hubc_take_my_name_changed(kj_hubc_t *h);

// 用 40 位随机数生成登记 token（base32：A-Z、2-7），out 至少 9 字节。
void kj_reg_token(uint32_t r1, uint32_t r2, char out[KJ_REG_TOKEN_LEN + 1]);
