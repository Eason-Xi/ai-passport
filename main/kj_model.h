// main/kj_model.h —— 把协议 / 界面状态整理成 kj_ui_model_t（纯 C，固件与电脑预览共用）。
#pragma once

#include "kj_flow.h"
#include "kj_server.h"
#include "kj_ui.h"

#include <stdbool.h>
#include <stdint.h>

void kj_model_title(kj_ui_model_t *m, const kj_flow_t *f, int battery, uint32_t now_ms);
void kj_model_player(kj_ui_model_t *m, const kj_flow_t *f, const kj_player_ctx_t *ctx, kj_page_t page,
                     int battery);
void kj_model_host(kj_ui_model_t *m, const kj_flow_t *f, const kj_server_t *s, uint32_t now_ms,
                   int battery, bool usb);
