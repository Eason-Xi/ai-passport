// main/kj_radio.h —— ESP-NOW 无线收发（Wi-Fi STA 模式，不连接任何路由器，固定信道）。
//
// 只在选定角色之后启动；接收回调运行在 Wi-Fi 任务里，只能拷贝入队。
// 发送只能从应用任务调用（内部维护一个最近使用的对端表，突破 ESP-NOW 20 个对端的上限）。
#pragma once

#include "esp_err.h"
#include "kj_proto.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_RADIO_CHANNEL 1

typedef void (*kj_radio_rx_cb_t)(const uint8_t mac[6], int8_t rssi, const uint8_t *data, int len);

esp_err_t kj_radio_start(kj_radio_rx_cb_t cb);
bool kj_radio_started(void);
esp_err_t kj_radio_send(const kj_out_t *it);
void kj_radio_get_mac(uint8_t mac[6]);
uint32_t kj_radio_tx_failures(void);
