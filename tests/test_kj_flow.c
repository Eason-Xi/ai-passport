// tests/test_kj_flow.c —— 界面状态机：页面推导、按键→动作、亮牌只播一次、通知 toast、选牌跳过用完的牌、
// 庄家菜单（不可用项跳过、危险操作二次确认）、界面模型的倒计时。
#include "kj_flow.h"
#include "kj_model.h"
#include "kj_test.h"

#include <string.h>

static kj_client_t c;
static kj_flow_t f;
static kj_opponent_t opps[4];
static kj_room_entry_t rooms[2];

static kj_player_ctx_t ctx(uint32_t now, int nopp)
{
    kj_player_ctx_t x = { .client = &c, .rooms = rooms, .room_count = 2, .opps = opps, .opp_count = nopp,
                          .connected = true, .now_ms = now };
    return x;
}

static kj_page_t update(uint32_t now, int nopp, kj_cue_t *cue)
{
    kj_player_ctx_t x = ctx(now, nopp);
    return kj_flow_player_update(&f, &x, cue);
}

static kj_action_t key(kj_key_t k, uint32_t now, int nopp)
{
    kj_player_ctx_t x = ctx(now, nopp);
    kj_flow_player_update(&f, &x, NULL);
    return kj_flow_player_key(&f, &x, k);
}

static void joined_view(uint8_t phase, uint8_t status)
{
    c.link = KJ_LINK_JOINED;
    c.room = 0x1234;
    c.view.no = 3;
    c.view.phase = phase;
    c.view.status = status;
    c.view.game_id = 1;
    for (int i = 0; i < 3; i++) c.view.cards[i] = 4;
    c.view.stars = 3;
    c.view.my_lock = KJ_CARD_NONE;
    c.view.res_my = c.view.res_opp = KJ_CARD_NONE;
}

static void test_title(void)
{
    kj_flow_init(&f, 1);
    CHECK_EQ(f.title_sel, 1);
    kj_action_t a = kj_flow_title_key(&f, KJ_KEY_DOWN);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(f.title_sel, 0);
    a = kj_flow_title_key(&f, KJ_KEY_OK);
    CHECK_EQ(a.kind, KJ_ACT_ROLE);
    CHECK_EQ(a.arg, 0);
}

static void test_player_pages(void)
{
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    rooms[0].room = 0xAAAA;
    rooms[1].room = 0xBBBB;
    kj_cue_t cue;
    CHECK_EQ(update(0, 0, &cue), KJ_PAGE_ROOMS);
    kj_action_t a = key(KJ_KEY_DOWN, 0, 0);
    CHECK_EQ(f.room_sel, 1);
    a = key(KJ_KEY_DOWN, 0, 0);
    CHECK_EQ(f.room_sel, 0);   // 循环
    a = key(KJ_KEY_UP, 0, 0);
    a = key(KJ_KEY_OK, 0, 0);
    CHECK_EQ(a.kind, KJ_ACT_JOIN);
    CHECK_EQ(a.room, 0xBBBB);
    a = key(KJ_KEY_OK_LONG, 0, 0);
    CHECK_EQ(a.kind, KJ_ACT_TO_TITLE);

    joined_view(KJ_PHASE_LOBBY, KJ_ST_WAITING);
    CHECK_EQ(update(10, 0, &cue), KJ_PAGE_SEAT);
    CHECK_EQ(cue, KJ_CUE_NONE);
    a = key(KJ_KEY_OK_LONG, 10, 0);
    CHECK_EQ(a.kind, KJ_ACT_LEAVE);

    c.view.phase = KJ_PHASE_RUNNING;
    c.view.status = KJ_ST_IDLE;
    CHECK_EQ(update(20, 2, &cue), KJ_PAGE_HAND);
    opps[0].no = 9;
    opps[1].no = 5;
    a = key(KJ_KEY_OK, 20, 2);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(update(21, 2, NULL), KJ_PAGE_OPPONENTS);
    key(KJ_KEY_DOWN, 22, 2);
    CHECK_EQ(f.opp_no, 5);
    // 列表重新排序：选中项跟着编号走
    opps[0].no = 5;
    opps[1].no = 9;
    update(23, 2, NULL);
    CHECK_EQ(f.opp_sel, 0);
    a = key(KJ_KEY_OK, 24, 2);
    CHECK_EQ(a.kind, KJ_ACT_REQUEST);
    CHECK_EQ(a.op, KJ_OP_CHALLENGE);
    CHECK_EQ(a.arg, 5);
    // 选中的人离开空闲名单：选中项收敛到剩下的人
    opps[0].no = 9;
    CHECK_EQ(update(25, 1, NULL), KJ_PAGE_OPPONENTS);
    CHECK_EQ(f.opp_no, 9);
    a = key(KJ_KEY_OK_LONG, 26, 1);
    CHECK_EQ(update(27, 1, NULL), KJ_PAGE_HAND);

    c.view.status = KJ_ST_CHALLENGING;
    c.view.peer_no = 5;
    CHECK_EQ(update(30, 0, &cue), KJ_PAGE_WAIT);
    a = key(KJ_KEY_OK_LONG, 31, 0);
    CHECK_EQ(a.op, KJ_OP_CANCEL);

    c.view.status = KJ_ST_CHALLENGED;
    CHECK_EQ(update(40, 0, &cue), KJ_PAGE_CHALLENGED);
    CHECK_EQ(cue, KJ_CUE_ALERT);
    CHECK_EQ(key(KJ_KEY_OK, 41, 0).op, KJ_OP_ACCEPT);
    CHECK_EQ(key(KJ_KEY_OK_LONG, 41, 0).op, KJ_OP_DECLINE);

    // 出牌：默认停在剪刀；用完的牌被跳过
    c.view.status = KJ_ST_DUEL;
    c.view.cards[KJ_SCISSORS] = 0;
    CHECK_EQ(update(50, 0, &cue), KJ_PAGE_CHOOSE);
    CHECK_EQ(cue, KJ_CUE_DUEL);
    CHECK_EQ(f.card_sel, KJ_PAPER);
    key(KJ_KEY_UP, 51, 0);
    CHECK_EQ(f.card_sel, KJ_ROCK);   // 跳过剪刀
    key(KJ_KEY_UP, 52, 0);
    CHECK_EQ(f.card_sel, KJ_PAPER);
    a = key(KJ_KEY_OK, 53, 0);
    CHECK_EQ(a.op, KJ_OP_PLAY);
    CHECK_EQ(a.arg, KJ_PAPER);
    CHECK_EQ(key(KJ_KEY_OK_LONG, 53, 0).op, KJ_OP_WITHDRAW);
    c.view.my_lock = KJ_PAPER;
    CHECK_EQ(key(KJ_KEY_OK, 54, 0).kind, KJ_ACT_NONE);   // 已扣牌，按键无效
    CHECK_EQ(key(KJ_KEY_OK_LONG, 54, 0).kind, KJ_ACT_NONE);

    // 亮牌：只播一次，OK 收起；新结果再次触发
    c.view.status = KJ_ST_IDLE;
    c.view.my_lock = KJ_CARD_NONE;
    c.view.res_duel_id = 7;
    c.view.res_outcome = KJ_OUT_WIN;
    c.view.res_my = KJ_PAPER;
    c.view.res_opp = KJ_ROCK;
    CHECK_EQ(update(60, 0, &cue), KJ_PAGE_REVEAL);
    CHECK_EQ(cue, KJ_CUE_WIN);
    CHECK_EQ(update(61, 0, &cue), KJ_PAGE_REVEAL);
    CHECK_EQ(cue, KJ_CUE_NONE);
    key(KJ_KEY_OK, 62, 0);
    CHECK_EQ(update(63, 0, &cue), KJ_PAGE_HAND);
    c.view.res_duel_id = 8;
    c.view.res_outcome = KJ_OUT_LOSE;
    CHECK_EQ(update(70, 0, &cue), KJ_PAGE_REVEAL);
    CHECK_EQ(cue, KJ_CUE_LOSE);
    CHECK_EQ(update(70 + KJ_REVEAL_AUTO_MS, 0, &cue), KJ_PAGE_HAND);   // 自动收起

    // 通知 → toast + 提示音；同一序号不重复
    c.view.notice = KJ_N_DECLINED;
    c.view.notice_seq++;
    update(20000, 0, &cue);
    CHECK_EQ(cue, KJ_CUE_NOTICE);
    CHECK_EQ(kj_flow_active_toast(&f, 20001), KJ_TOAST_DECLINED);
    CHECK_EQ(kj_flow_active_toast(&f, 20000 + KJ_TOAST_MS), KJ_TOAST_NONE);
    update(20002, 0, &cue);
    CHECK_EQ(cue, KJ_CUE_NONE);

    // 终局
    c.view.status = KJ_ST_CLEARED;
    CHECK_EQ(update(30000, 0, &cue), KJ_PAGE_FINAL);
    CHECK_EQ(cue, KJ_CUE_CLEARED);
    // 新一局：回到入座页，旧结算不重播
    c.view.game_id = 2;
    c.view.phase = KJ_PHASE_LOBBY;
    c.view.status = KJ_ST_WAITING;
    CHECK_EQ(update(31000, 0, &cue), KJ_PAGE_SEAT);

    // 重新入座时，视图里已有的历史结算 / 通知不会重播
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    update(0, 0, NULL);
    joined_view(KJ_PHASE_RUNNING, KJ_ST_IDLE);
    c.view.res_duel_id = 99;
    c.view.res_outcome = KJ_OUT_WIN;
    c.view.notice = KJ_N_BUSY;
    c.view.notice_seq = 4;
    CHECK_EQ(update(10, 0, &cue), KJ_PAGE_HAND);
    CHECK_EQ(cue, KJ_CUE_NONE);
    CHECK_EQ(kj_flow_active_toast(&f, 11), KJ_TOAST_NONE);
}

static void test_valid_card(void)
{
    kj_view_t v = { 0 };
    CHECK_EQ(kj_flow_valid_card(&v, 0, 1), KJ_CARD_NONE);
    v.cards[KJ_PAPER] = 1;
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, 1), KJ_PAPER);
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, -1), KJ_PAPER);
    v.cards[KJ_SCISSORS] = 2;
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, 1), KJ_SCISSORS);
    CHECK_EQ(kj_flow_valid_card(&v, KJ_ROCK, -1), KJ_PAPER);
}

static void test_host_menu(void)
{
    static kj_game_t g;
    kj_rules_init(&g);
    kj_flow_init(&f, 1);
    CHECK(!kj_flow_host_item_enabled(KJ_HM_START, &g));
    CHECK(kj_flow_host_item_enabled(KJ_HM_BOT_ADD, &g));
    CHECK(!kj_flow_host_item_enabled(KJ_HM_BOT_DEL, &g));
    // 人不够时开局被拒，提示需要 2 人
    kj_action_t a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 100);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(a.arg, 1);
    CHECK_EQ(kj_flow_active_toast(&f, 101), KJ_TOAST_NEED_TWO);
    kj_flow_host_sync(&f, &g);
    CHECK_EQ(f.host_sel, KJ_HM_BOT_ADD);   // 顺延到第一个可用项
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 200);
    CHECK_EQ(a.kind, KJ_ACT_HOST_CMD);
    CHECK_EQ(a.cmd, KJ_CMD_BOT_ADD);
    kj_rules_add_bot(&g, 0);
    kj_rules_add_bot(&g, 0);
    // ▲ 从"添加电脑"往上跳过"新一局 / 结束"（不可用），落在"开始"
    a = kj_flow_host_key(&f, &g, KJ_KEY_UP, 300);
    CHECK_EQ(f.host_sel, KJ_HM_START);
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 300);
    CHECK_EQ(a.cmd, KJ_CMD_START);
    kj_rules_start(&g, 300);
    kj_flow_host_sync(&f, &g);
    CHECK_EQ(f.host_sel, KJ_HM_END);
    // 结束需要二次确认；确认时其他键取消
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 400);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(f.host_confirm, KJ_HM_END);
    a = kj_flow_host_key(&f, &g, KJ_KEY_DOWN, 401);
    CHECK_EQ(a.kind, KJ_ACT_NONE);
    CHECK_EQ(f.host_confirm, -1);
    CHECK_EQ(f.host_sel, KJ_HM_END);      // 取消确认的那一下不移动选中项
    kj_flow_host_key(&f, &g, KJ_KEY_OK, 403);
    a = kj_flow_host_key(&f, &g, KJ_KEY_OK, 404);
    CHECK_EQ(a.kind, KJ_ACT_HOST_CMD);
    CHECK_EQ(a.cmd, KJ_CMD_END);
    // 确认中的项失效时自动取消
    f.host_confirm = KJ_HM_START;
    kj_flow_host_sync(&f, &g);
    CHECK_EQ(f.host_confirm, -1);
}

static void test_model(void)
{
    memset(&c, 0, sizeof(c));
    kj_flow_init(&f, 0);
    joined_view(KJ_PHASE_RUNNING, KJ_ST_CHALLENGED);
    c.view.deadline_s = 18;
    c.view_rx_ms = 1000;
    c.have_room_info = true;
    c.room_info.seated = 7;
    kj_player_ctx_t x = ctx(4500, 0);
    kj_page_t page = kj_flow_player_update(&f, &x, NULL);
    kj_ui_model_t m;
    kj_model_player(&m, &f, &x, page, 150);
    CHECK_EQ(m.page, KJ_PAGE_CHALLENGED);
    CHECK_EQ(m.deadline_s, 15);   // 收到视图后过了 3.5 s
    CHECK_EQ(m.battery, 100);
    CHECK_EQ(m.seated, 7);
    CHECK(!m.disconnected);
    x.now_ms = 60000;
    kj_model_player(&m, &f, &x, page, -5);
    CHECK_EQ(m.deadline_s, 0);
    CHECK_EQ(m.battery, -1);

    static kj_server_t s;
    kj_server_init(&s, 0xBEEF, 1);
    kj_server_command(&s, KJ_CMD_BOT_ADD, 0, 0);
    kj_model_host(&m, &f, &s, 65000, 50, true);
    CHECK_EQ(m.page, KJ_PAGE_HOST);
    CHECK_EQ(m.host_room, 0xBEEF);
    CHECK_EQ(m.host_sum.seated, 1);
    CHECK_EQ(m.host_phase_s, 65);
    CHECK(m.host_enabled & (1u << KJ_HM_BOT_DEL));
    CHECK(!(m.host_enabled & (1u << KJ_HM_START)));
}

int main(void)
{
    test_title();
    test_player_pages();
    test_valid_card();
    test_host_menu();
    test_model();
    KJ_TEST_DONE("test_kj_flow");
}
