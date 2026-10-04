// main/kj_store.c —— NVS 读写（命名空间 "kjrps"）。
#include "kj_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "kj_store";
static const char *NS = "kjrps";
static bool s_ready;

esp_err_t kj_store_init(void)
{
    // 不擦除：初始化失败时只是不保存（角色 / 赌局快照 / 射频校准都会退化为不缓存），
    // 绝不为了让初始化通过而清掉用户数据。
    esp_err_t err = nvs_flash_init();
    s_ready = err == ESP_OK;
    if (!s_ready) ESP_LOGE(TAG, "NVS init failed (%s); running without persistence", esp_err_to_name(err));
    return err;
}

static uint32_t get_u32(const char *key, uint32_t def)
{
    nvs_handle_t h;
    uint32_t v = def;
    if (!s_ready || nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return def;
    if (nvs_get_u32(h, key, &v) != ESP_OK) v = def;
    nvs_close(h);
    return v;
}

static void set_u32(const char *key, uint32_t v)
{
    nvs_handle_t h;
    if (!s_ready || nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    uint32_t old;
    if (nvs_get_u32(h, key, &old) != ESP_OK || old != v) {
        if (nvs_set_u32(h, key, v) == ESP_OK) nvs_commit(h);
    }
    nvs_close(h);
}

uint8_t kj_store_get_role(void) { return get_u32("role", 0) ? 1 : 0; }
void kj_store_set_role(uint8_t role) { set_u32("role", role ? 1 : 0); }
uint16_t kj_store_get_room(void) { return (uint16_t)get_u32("room", 0); }
void kj_store_set_room(uint16_t room) { set_u32("room", room); }

esp_err_t kj_store_save_game(const uint8_t *blob, size_t len)
{
    nvs_handle_t h;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "game", blob, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

size_t kj_store_load_game(uint8_t *blob, size_t cap)
{
    nvs_handle_t h;
    if (!s_ready || nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t len = cap;
    esp_err_t err = nvs_get_blob(h, "game", blob, &len);
    nvs_close(h);
    return err == ESP_OK ? len : 0;
}
