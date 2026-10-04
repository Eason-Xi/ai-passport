// main/kj_flow.h —— 选手 / 庄家设备的界面状态机：由协议状态推导当前页面，
// 把三个按键翻译成请求或本地动作，并产出提示音、toast。纯 C，主机测试见 tests/test_kj_flow.c。
//
// 按键约定（全应用统一）：
//   ▲ / ▼ 按下   在列表 / 牌之间移动（按下即响应）
//   OK   单击   确认当前页面的主要动作
//   OK   长按   返回 / 撤回 / 拒绝 / 放弃（每页底部有提示）
#pragma once

#include "kj_client.h"
#include "kj_rules.h"
#include "kj_server.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KJ_KEY_UP = 0,
    KJ_KEY_DOWN,
    KJ_KEY_OK,
    KJ_KEY_OK_LONG,
} kj_key_t;

typedef enum {
    KJ_PAGE_TITLE = 0,   // 选择角色
    KJ_PAGE_ROOMS,       // 找赌局
    KJ_PAGE_SEAT,        // 已入座，等待开局 / 下一局
    KJ_PAGE_HAND,        // 手牌主页（空闲）
    KJ_PAGE_OPPONENTS,   // 选择对手
    KJ_PAGE_WAIT,        // 已发出挑战，等待应战
    KJ_PAGE_CHALLENGED,  // 收到挑战
    KJ_PAGE_CHOOSE,      // 出牌
    KJ_PAGE_REVEAL,      // 亮牌结算
    KJ_PAGE_FINAL,       // 过关 / 出局 / 失败
    KJ_PAGE_HOST,        // 庄家面板
    KJ_PAGE_COUNT,
} kj_page_t;

typedef enum {
    KJ_CUE_NONE = 0,
    KJ_CUE_KEY,          // 轻点
    KJ_CUE_ALERT,        // 收到挑战
    KJ_CUE_DUEL,         // 对决开始
    KJ_CUE_LOCK,         // 出牌
    KJ_CUE_WIN,
    KJ_CUE_LOSE,
    KJ_CUE_DRAW,
    KJ_CUE_CLEARED,
    KJ_CUE_OUT,          // 出局 / 失败
    KJ_CUE_NOTICE,       // toast
    KJ_CUE_ERROR,
    KJ_CUE_COUNT,
} kj_cue_t;

typedef enum {
    KJ_TOAST_NONE = 0,
    KJ_TOAST_DECLINED, KJ_TOAST_CANCELLED, KJ_TOAST_TIMEOUT, KJ_TOAST_WITHDRAWN, KJ_TOAST_ABORTED,
    KJ_TOAST_BUSY, KJ_TOAST_NOT_RUNNING, KJ_TOAST_NO_CARD, KJ_TOAST_INVALID, KJ_TOAST_FULL,
    KJ_TOAST_KICKED, KJ_TOAST_NO_REPLY, KJ_TOAST_RESTORED, KJ_TOAST_NEED_TWO, KJ_TOAST_DONE,
    KJ_TOAST_RADIO_FAIL,
    KJ_TOAST_COUNT,
} kj_toast_t;

#define KJ_TOAST_MS        2600
#define KJ_REVEAL_AUTO_MS  12000   // 亮牌页无人操作也会在这么久后自动收起

typedef enum {
    KJ_ACT_NONE = 0,
    KJ_ACT_REQUEST,      // 发送 op/arg 请求
    KJ_ACT_JOIN,         // 入座 room
    KJ_ACT_LEAVE,        // 本地离座，回到找赌局
    KJ_ACT_TO_TITLE,     // 回到选择角色
    KJ_ACT_ROLE,         // 首页选定角色：arg = 0 选手 / 1 庄家
    KJ_ACT_HOST_CMD,     // 庄家命令：cmd
} kj_action_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t op;
    uint8_t arg;
    uint16_t room;
    kj_cmd_t cmd;
} kj_action_t;

// 选手侧每轮的输入快照（由平台层从 kj_client_t 生成）。
typedef struct {
    const kj_client_t *client;
    const kj_room_entry_t *rooms;
    int room_count;
    const kj_opponent_t *opps;
    int opp_count;
    bool connected;
    uint32_t now_ms;
} kj_player_ctx_t;

typedef enum {
    KJ_HM_START = 0, KJ_HM_END, KJ_HM_NEW, KJ_HM_BOT_ADD, KJ_HM_BOT_DEL, KJ_HM_RESET, KJ_HM_COUNT,
} kj_host_item_t;

typedef struct {
    // 首页
    uint8_t title_sel;
    // 选手
    uint8_t room_sel;
    uint8_t opp_sel;
    uint8_t opp_no;            // 记住选中的编号：列表重新排序时选中项不跳
    uint8_t card_sel;
    bool picking;              // 在选择对手页
    uint16_t shown_res_duel;   // 已经看过的亮牌（对决编号）
    bool reveal_active;
    uint32_t reveal_since;
    uint16_t game_id;
    uint16_t room;
    uint8_t notice_seq;
    uint8_t last_status;
    uint8_t last_page;
    bool primed;               // 已同步过一次视图（首次入座不对历史通知响铃）
    // toast
    uint8_t toast;
    uint32_t toast_until;
    // 庄家
    uint8_t host_sel;
    int8_t host_confirm;       // 等待确认的菜单项，-1 = 无
} kj_flow_t;

void kj_flow_init(kj_flow_t *f, uint8_t title_sel);
void kj_flow_toast(kj_flow_t *f, kj_toast_t t, uint32_t now_ms);
kj_toast_t kj_flow_active_toast(const kj_flow_t *f, uint32_t now_ms);
kj_toast_t kj_flow_toast_for_notice(uint8_t notice);

// 首页
kj_action_t kj_flow_title_key(kj_flow_t *f, kj_key_t key);

// 选手：每轮调用，返回应显示的页面，*cue 写入需要播放的提示音（可为 NULL）。
kj_page_t kj_flow_player_update(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_cue_t *cue);
kj_action_t kj_flow_player_key(kj_flow_t *f, const kj_player_ctx_t *ctx, kj_key_t key);
// 当前出牌页可选的牌（跳过已用完的）；没有可选返回 KJ_CARD_NONE。
uint8_t kj_flow_valid_card(const kj_view_t *v, uint8_t preferred, int direction);

// 庄家
bool kj_flow_host_item_enabled(kj_host_item_t item, const kj_game_t *g);
kj_action_t kj_flow_host_key(kj_flow_t *f, const kj_game_t *g, kj_key_t key, uint32_t now_ms);
// 每轮调用：选中项不可用时顺延到下一个可用项；确认中的项失效时取消确认。
void kj_flow_host_sync(kj_flow_t *f, const kj_game_t *g);
