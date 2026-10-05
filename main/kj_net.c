// main/kj_net.c —— Wi-Fi STA、hub UDP 中继与庄家看板 TCP（平台层）。
#include "kj_net.h"
#include "kj_hubproto.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "kj_net";

#define NET_TASK_STACK      4096
#define NET_TASK_PRIO       6
#define SELECT_MS           100
#define RSSI_POLL_MS        5000
#define WIFI_BACKOFF_MIN_MS 1000
#define WIFI_BACKOFF_MAX_MS 10000
#define BOARD_SB_BYTES      4096
#define BOARD_TX_CHUNK      512
#define BOARD_IDLE_MS       8000    // hub 每 2 s 发一个空行；这么久没收到就重连
#define BOARD_BACKOFF_MAX   5000
#define BOARD_LINE_MAX      96      // hub → 设备的命令行（"@KJ kick 128" 之类）

static kj_net_cbs_t s_cbs;
static bool s_started;
static uint8_t s_mac[6];
static bool s_mac_read;
static volatile bool s_has_ip;
static volatile uint32_t s_ip;
static volatile int8_t s_rssi;
static volatile bool s_host;
static volatile uint32_t s_hub_ip;
static volatile uint16_t s_hub_tcp_port;
static int s_udp = -1;
static uint16_t s_seq;
static esp_timer_handle_t s_retry_timer;
static uint32_t s_backoff_ms = WIFI_BACKOFF_MIN_MS;

// 看板 TCP：只有 kj_net 任务碰 socket；应用任务只写 stream buffer。
static StreamBufferHandle_t s_board_sb;
static volatile bool s_board_on, s_board_up, s_board_overflow;
static int s_tcp = -1;
static bool s_tcp_connecting;
static uint32_t s_tcp_next_ms, s_tcp_backoff_ms = 500, s_tcp_last_rx_ms;
static uint8_t s_tx[BOARD_TX_CHUNK];
static size_t s_tx_len, s_tx_off;
static char s_line[BOARD_LINE_MAX];
static size_t s_line_len;
static bool s_line_drop;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void kj_net_get_mac(uint8_t mac[6])
{
    if (!s_mac_read) {
        esp_read_mac(s_mac, ESP_MAC_WIFI_STA);
        s_mac_read = true;
    }
    memcpy(mac, s_mac, 6);
}

bool kj_net_started(void) { return s_started; }
bool kj_net_has_ip(void) { return s_has_ip; }
uint32_t kj_net_ip(void) { return s_has_ip ? s_ip : 0; }
int8_t kj_net_ap_rssi(void) { return s_rssi; }
void kj_net_set_host(bool host) { s_host = host; }

void kj_net_set_hub(uint32_t ip, uint16_t tcp_port)
{
    s_hub_ip = ip;
    s_hub_tcp_port = tcp_port;
}

// ---------------------------------------------------------------------------
// Wi-Fi 事件（运行在默认事件循环任务里：只改状态、调回调，不阻塞）
// ---------------------------------------------------------------------------

static void retry_connect(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        s_has_ip = false;
        s_ip = 0;
        s_rssi = 0;
        if (s_cbs.on_event) s_cbs.on_event(KJ_NET_EV_DISCONNECTED, d ? d->reason : 0);
        uint32_t delay = s_backoff_ms;
        s_backoff_ms = s_backoff_ms * 2 > WIFI_BACKOFF_MAX_MS ? WIFI_BACKOFF_MAX_MS : s_backoff_ms * 2;
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)delay * 1000u);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        s_ip = e->ip_info.ip.addr;
        s_has_ip = true;
        s_backoff_ms = WIFI_BACKOFF_MIN_MS;
        if (s_cbs.on_event) s_cbs.on_event(KJ_NET_EV_GOT_IP, 0);
    }
}

// ---------------------------------------------------------------------------
// UDP
// ---------------------------------------------------------------------------

static int open_udp(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(KH_PORT_DEVICE) };
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

static void udp_drain(uint8_t *buf)
{
    for (int k = 0; k < 16; k++) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s_udp, buf, KH_DGRAM_MAX, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
        if (n <= 0) return;
        kh_env_t e;
        if (!kh_env_decode(buf, (size_t)n, &e) || memcmp(e.src, s_mac, 6) == 0) continue;
        if (e.kind == KH_K_FRAME) {
            if (e.len >= 1 && e.len <= KJ_FRAME_MAX && s_cbs.on_frame) s_cbs.on_frame(e.src, e.rssi, e.payload, e.len);
        } else if (s_cbs.on_ctl) {
            s_cbs.on_ctl(buf, n, from.sin_addr.s_addr);
        }
    }
}

static esp_err_t send_dgram(uint32_t ip, const kh_env_t *e)
{
    if (s_udp < 0 || !s_has_ip) return ESP_ERR_INVALID_STATE;
    uint8_t buf[KH_HDR + 64];
    size_t n = kh_env_encode(e, buf, sizeof(buf));
    if (n == 0) return ESP_ERR_INVALID_SIZE;
    struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(KH_PORT_HUB) };
    to.sin_addr.s_addr = ip ? ip : htonl(INADDR_BROADCAST);
    int r = sendto(s_udp, buf, n, 0, (struct sockaddr *)&to, sizeof(to));
    return r == (int)n ? ESP_OK : ESP_FAIL;   // 缓冲区满等失败直接丢：协议层会重发
}

esp_err_t kj_net_send_frame(const kj_out_t *it)
{
    if (!s_hub_ip) return ESP_ERR_INVALID_STATE;
    kh_env_t e = { .kind = KH_K_FRAME, .seq = ++s_seq, .rssi = s_rssi, .flags = s_host ? KH_FLAG_HOST : 0,
                   .payload = it->data, .len = it->len };
    if (it->len >= KJ_FRAME_HEADER) e.room = (uint16_t)(it->data[4] | (it->data[5] << 8));
    memcpy(e.src, s_mac, 6);
    memcpy(e.dst, it->broadcast ? KH_MAC_BROADCAST : it->mac, 6);
    return send_dgram(s_hub_ip, &e);
}

esp_err_t kj_net_send_ctl(uint8_t kind, uint32_t ip, uint16_t room, const uint8_t *data, size_t len)
{
    if (len > 64) return ESP_ERR_INVALID_SIZE;
    kh_env_t e = { .kind = kind, .room = room, .seq = ++s_seq, .rssi = s_rssi,
                   .flags = s_host ? KH_FLAG_HOST : 0, .payload = data, .len = (uint16_t)len };
    memcpy(e.src, s_mac, 6);
    memcpy(e.dst, KH_MAC_HUB, 6);
    return send_dgram(ip, &e);
}

// ---------------------------------------------------------------------------
// 看板 TCP（只在 kj_net 任务里操作 socket）
// ---------------------------------------------------------------------------

void kj_net_board_enable(bool on)
{
    if (on && !s_board_sb) s_board_sb = xStreamBufferCreate(BOARD_SB_BYTES, 1);
    s_board_on = on && s_board_sb;
}

bool kj_net_board_connected(void) { return s_board_up; }

void kj_net_board_write(const char *line, size_t n)
{
    if (!s_board_up || !s_board_sb || n == 0) return;   // 没连上就不攒：连上后调用方补发全量
    if (xStreamBufferSpacesAvailable(s_board_sb) < n) {  // 只有一个写者：先看空间，保证整行写入
        s_board_overflow = true;
        return;
    }
    xStreamBufferSend(s_board_sb, line, n, 0);
}

bool kj_net_board_take_overflow(void)
{
    bool v = s_board_overflow;
    s_board_overflow = false;
    return v;
}

static void board_close(uint32_t now)
{
    if (s_tcp >= 0) close(s_tcp);
    s_tcp = -1;
    s_tcp_connecting = false;
    s_line_len = 0;
    s_line_drop = false;
    s_tx_len = s_tx_off = 0;
    bool was_up = s_board_up;
    s_board_up = false;
    if (s_board_sb) xStreamBufferReset(s_board_sb);
    s_tcp_next_ms = now + s_tcp_backoff_ms;
    s_tcp_backoff_ms = s_tcp_backoff_ms * 2 > BOARD_BACKOFF_MAX ? BOARD_BACKOFF_MAX : s_tcp_backoff_ms * 2;
    if (was_up && s_cbs.on_event) s_cbs.on_event(KJ_NET_EV_BOARD_DOWN, 0);
}

static void board_up(uint32_t now)
{
    s_tcp_connecting = false;
    s_tcp_backoff_ms = 500;
    s_tcp_last_rx_ms = now;
    int yes = 1;
    setsockopt(s_tcp, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    setsockopt(s_tcp, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    if (s_board_sb) xStreamBufferReset(s_board_sb);
    s_board_up = true;
    if (s_cbs.on_event) s_cbs.on_event(KJ_NET_EV_BOARD_UP, 0);
}

static void board_prepare(fd_set *rfds, fd_set *wfds, int *maxfd, uint32_t now)
{
    bool want = s_board_on && s_has_ip && s_hub_ip && s_hub_tcp_port;
    if (!want) {
        if (s_tcp >= 0) board_close(now);
        return;
    }
    if (s_tcp < 0 && (int32_t)(now - s_tcp_next_ms) >= 0) {
        s_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s_tcp < 0) {
            board_close(now);
            return;
        }
        fcntl(s_tcp, F_SETFL, fcntl(s_tcp, F_GETFL, 0) | O_NONBLOCK);
        struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(s_hub_tcp_port) };
        to.sin_addr.s_addr = s_hub_ip;
        if (connect(s_tcp, (struct sockaddr *)&to, sizeof(to)) == 0) {
            board_up(now);
        } else if (errno == EINPROGRESS) {
            s_tcp_connecting = true;
        } else {
            board_close(now);
            return;
        }
    }
    if (s_tcp < 0) return;
    FD_SET(s_tcp, rfds);
    if (s_tcp_connecting || s_tx_off < s_tx_len || (s_board_sb && xStreamBufferBytesAvailable(s_board_sb) > 0)) {
        FD_SET(s_tcp, wfds);
    }
    if (s_tcp > *maxfd) *maxfd = s_tcp;
}

static void board_rx(uint32_t now)
{
    char buf[64];
    int n = recv(s_tcp, buf, sizeof(buf), MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        board_close(now);
        return;
    }
    if (n < 0) return;
    s_tcp_last_rx_ms = now;
    for (int i = 0; i < n; i++) {
        char ch = buf[i];
        if (ch == '\n' || ch == '\r') {
            if (s_line_len && !s_line_drop && s_cbs.on_line) {
                s_line[s_line_len] = '\0';
                s_cbs.on_line(s_line, (int)s_line_len);
            }
            s_line_len = 0;
            s_line_drop = false;
        } else if (s_line_len < sizeof(s_line) - 1) {
            s_line[s_line_len++] = ch;
        } else {
            s_line_drop = true;   // 超长行：丢到下一个换行为止
        }
    }
}

static void board_tx(uint32_t now)
{
    if (s_tx_off >= s_tx_len) {
        s_tx_len = s_board_sb ? xStreamBufferReceive(s_board_sb, s_tx, sizeof(s_tx), 0) : 0;
        s_tx_off = 0;
    }
    if (s_tx_off >= s_tx_len) return;
    int n = send(s_tcp, s_tx + s_tx_off, s_tx_len - s_tx_off, MSG_DONTWAIT);
    if (n > 0) {
        s_tx_off += (size_t)n;
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        board_close(now);
    }
}

static void board_service(const fd_set *rfds, const fd_set *wfds, uint32_t now)
{
    if (s_tcp < 0) return;
    if (s_tcp_connecting) {
        if (!FD_ISSET(s_tcp, wfds)) return;
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(s_tcp, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
            board_close(now);
            return;
        }
        board_up(now);
    }
    if (FD_ISSET(s_tcp, rfds)) board_rx(now);
    if (s_tcp >= 0 && FD_ISSET(s_tcp, wfds)) board_tx(now);
    if (s_tcp >= 0 && !s_tcp_connecting && (uint32_t)(now - s_tcp_last_rx_ms) >= BOARD_IDLE_MS) board_close(now);
}

// ---------------------------------------------------------------------------
// 收包任务
// ---------------------------------------------------------------------------

static void net_task(void *arg)
{
    (void)arg;
    static uint8_t buf[KH_DGRAM_MAX];
    uint32_t rssi_ms = 0;
    for (;;) {
        uint32_t now = now_ms();
        if (s_udp < 0) s_udp = open_udp();
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        int maxfd = -1;
        if (s_udp >= 0) {
            FD_SET(s_udp, &rfds);
            maxfd = s_udp;
        }
        board_prepare(&rfds, &wfds, &maxfd, now);
        int r = 0;
        if (maxfd >= 0) {
            struct timeval tv = { .tv_sec = 0, .tv_usec = SELECT_MS * 1000 };
            r = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
        } else {
            vTaskDelay(pdMS_TO_TICKS(SELECT_MS));
        }
        now = now_ms();
        if (r > 0) {
            if (s_udp >= 0 && FD_ISSET(s_udp, &rfds)) udp_drain(buf);
            board_service(&rfds, &wfds, now);
        } else if (s_tcp >= 0 && !s_tcp_connecting && (uint32_t)(now - s_tcp_last_rx_ms) >= BOARD_IDLE_MS) {
            board_close(now);
        }
        if (s_has_ip && (uint32_t)(now - rssi_ms) >= RSSI_POLL_MS) {
            rssi_ms = now;
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_rssi = ap.rssi;
        }
    }
}

// ---------------------------------------------------------------------------
// 启动
// ---------------------------------------------------------------------------

esp_err_t kj_net_start(const kj_wifi_cred_t *cred, const kj_net_cbs_t *cbs)
{
    if (s_started) return ESP_OK;
    if (!cred || !cred->ssid[0]) return ESP_ERR_INVALID_ARG;
    s_cbs = *cbs;
    uint8_t mac[6];
    kj_net_get_mac(mac);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_ERR_NO_MEM;
    char host[16];
    snprintf(host, sizeof(host), "kj-%02x%02x", mac[4], mac[5]);   // 路由器客户端列表里认得出来
    esp_netif_set_hostname(netif, host);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;   // 凭据由 kj_store 自己保存
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    const esp_timer_create_args_t targs = { .callback = retry_connect, .name = "kj_wifi_retry" };
    err = esp_timer_create(&targs, &s_retry_timer);
    if (err != ESP_OK) return err;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);

    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, cred->ssid, strnlen(cred->ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, cred->pass, strnlen(cred->pass, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = cred->pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;      // 多个 AP 时连信号最强的那个
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;
#if CONFIG_KJ_WIFI_PS_MIN_MODEM
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#else
    esp_wifi_set_ps(WIFI_PS_NONE);   // 省电模式会把每个包的延迟拉到几百毫秒
#endif
    if (xTaskCreate(net_task, "kj_net", NET_TASK_STACK, NULL, NET_TASK_PRIO, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    s_started = true;
    ESP_LOGI(TAG, "wifi started (ssid len %u)", (unsigned)strlen(cred->ssid));   // 不打印 SSID / 密码
    return ESP_OK;
}
