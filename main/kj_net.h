// main/kj_net.h —— Wi-Fi STA + 与电脑 hub 的 UDP / TCP 通信（平台层，依赖 ESP-IDF）。
//
//   * 连现场路由器（凭据由调用方给出；断线后 1 → 2 → 4 … 10 s 退避重连）。
//   * UDP 绑定 KH_PORT_DEVICE：收包任务 kj_net 解开 hub 信封，游戏帧交给 on_frame，
//     其余控制消息原样交给 on_ctl；回调只能拷贝入队（运行在 kj_net 任务里）。
//   * 发送在调用方任务里直接 sendto（lwIP 允许一个任务收、另一个任务发同一个 socket）。
//   * 庄家看板：打开后由 kj_net 任务独占一条到 hub 的 TCP 连接，承载 @KJ 行；hub 发来的命令行交给
//     on_line。连接断开时丢弃待发数据（重连后由调用方补发全量）。
#pragma once

#include "esp_err.h"
#include "kj_proto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KJ_WIFI_SSID_MAX 32
#define KJ_WIFI_PASS_MAX 64

typedef struct {
    char ssid[KJ_WIFI_SSID_MAX + 1];
    char pass[KJ_WIFI_PASS_MAX + 1];
} kj_wifi_cred_t;

typedef enum {
    KJ_NET_EV_GOT_IP = 1,
    KJ_NET_EV_DISCONNECTED,   // arg = wifi_err_reason_t
    KJ_NET_EV_BOARD_UP,       // 看板 TCP 连上
    KJ_NET_EV_BOARD_DOWN,
} kj_net_event_t;

typedef struct {
    void (*on_frame)(const uint8_t src[6], int8_t rssi, const uint8_t *data, int len);
    void (*on_ctl)(const uint8_t *dgram, int len, uint32_t src_ip);
    void (*on_event)(uint8_t ev, int arg);
    void (*on_line)(const char *line, int len);
} kj_net_cbs_t;

esp_err_t kj_net_start(const kj_wifi_cred_t *cred, const kj_net_cbs_t *cbs);
bool kj_net_started(void);
bool kj_net_has_ip(void);
uint32_t kj_net_ip(void);                // IPv4，网络字节序（与 sin_addr.s_addr 相同）；0 = 没有
void kj_net_get_mac(uint8_t mac[6]);     // STA MAC（kj_net_start 之前也可调用）
int8_t kj_net_ap_rssi(void);             // 到路由器的信号强度（约 5 s 刷新一次），0 = 未知
void kj_net_set_host(bool host);         // 信封里标记"我是庄家"
void kj_net_set_hub(uint32_t ip, uint16_t tcp_port);
// 发游戏帧（目标由 it->mac / it->broadcast 决定，经 hub 中继）。没有 IP 或还没找到 hub 时返回错误。
esp_err_t kj_net_send_frame(const kj_out_t *it);
// 发控制消息给 hub：ip = 0 表示广播。
esp_err_t kj_net_send_ctl(uint8_t kind, uint32_t ip, uint16_t room, const uint8_t *data, size_t len);
// 庄家看板
void kj_net_board_enable(bool on);
bool kj_net_board_connected(void);
void kj_net_board_write(const char *line, size_t n);   // 连上时整行入队，放不下则丢弃并记录
bool kj_net_board_take_overflow(void);                 // 有行被丢弃过：调用方应补发全量
