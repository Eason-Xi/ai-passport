// main/kj_model.c —— 界面模型组装（纯 C）。
#include "kj_model.h"

#include <string.h>

static int8_t clamp_battery(int battery)
{
    if (battery < 0) return -1;
    return (int8_t)(battery > 100 ? 100 : battery);
}

void kj_model_title(kj_ui_model_t *m, const kj_flow_t *f, int battery, uint32_t now_ms)
{
    memset(m, 0, sizeof(*m));
    m->page = KJ_PAGE_TITLE;
    m->battery = clamp_battery(battery);
    m->now_ms = now_ms;
    m->title_sel = f->title_sel;
    m->toast = (uint8_t)kj_flow_active_toast(f, now_ms);
    m->host_confirm = -1;
}

void kj_model_player(kj_ui_model_t *m, const kj_flow_t *f, const kj_player_ctx_t *ctx, kj_page_t page,
                     int battery)
{
    memset(m, 0, sizeof(*m));
    const kj_client_t *c = ctx->client;
    m->page = (uint8_t)page;
    m->battery = clamp_battery(battery);
    m->now_ms = ctx->now_ms;
    m->toast = (uint8_t)kj_flow_active_toast(f, ctx->now_ms);
    m->host_confirm = -1;
    int rooms = ctx->room_count < KJ_CLIENT_MAX_ROOMS ? ctx->room_count : KJ_CLIENT_MAX_ROOMS;
    for (int i = 0; i < rooms; i++) {
        m->rooms[i] = ctx->rooms[i];
        m->rooms[i].seen_ms = 0;
    }
    m->room_count = (uint8_t)(rooms > 0 ? rooms : 0);
    m->room_sel = f->room_sel;
    m->joining = c->link == KJ_LINK_JOINING;
    if (c->link != KJ_LINK_JOINED) return;
    m->room = c->room;
    m->view = c->view;
    m->seated = c->have_room_info ? c->room_info.seated : 0;
    m->disconnected = !ctx->connected;
    int n = ctx->opp_count < KJ_UI_OPP_MAX ? ctx->opp_count : KJ_UI_OPP_MAX;
    for (int i = 0; i < n; i++) m->opps[i] = ctx->opps[i];
    m->opp_count = (uint8_t)(n > 0 ? n : 0);
    m->opp_sel = f->opp_sel < m->opp_count ? f->opp_sel : 0;
    m->card_sel = f->card_sel;
    // 倒计时按收到视图后的本地流逝时间实时递减。
    uint32_t el = (ctx->now_ms - c->view_rx_ms) / 1000u;
    m->deadline_s = (uint8_t)(c->view.deadline_s > el ? c->view.deadline_s - el : 0);
}

void kj_model_host(kj_ui_model_t *m, const kj_flow_t *f, const kj_server_t *s, uint32_t now_ms,
                   int battery, bool usb)
{
    memset(m, 0, sizeof(*m));
    m->page = KJ_PAGE_HOST;
    m->battery = clamp_battery(battery);
    m->now_ms = now_ms;
    m->toast = (uint8_t)kj_flow_active_toast(f, now_ms);
    m->host_room = s->room;
    m->host_phase = s->game.phase;
    m->host_phase_s = (now_ms - s->game.phase_since_ms) / 1000u;
    kj_rules_summary(&s->game, now_ms, &m->host_sum);
    m->host_sel = f->host_sel;
    m->host_confirm = f->host_confirm;
    for (int i = 0; i < KJ_HM_COUNT; i++) {
        if (kj_flow_host_item_enabled((kj_host_item_t)i, &s->game)) m->host_enabled |= (uint8_t)(1u << i);
    }
    m->usb = usb;
}
