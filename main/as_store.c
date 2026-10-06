// main/as_store.c —— 设置持久化，说明见 as_store.h。
#include "as_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "as_store";

#define NAMESPACE "asmr"
#define KEY_CFG "cfg"
#define SAVE_DELAY_MS 1500

static bool s_ok;          // NVS 可用
static bool s_dirty;
static uint32_t s_changed_ms;

void as_store_init(as_cfg_t *cfg) {
    as_cfg_default(cfg);
    const esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) {
        // 不自动擦除：分区里可能有别的数据，宁可本次不保存。
        ESP_LOGE(TAG, "nvs_flash_init 失败（%s），本次运行不保存设置", esp_err_to_name(e));
        return;
    }
    s_ok = true;
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "尚无存档，使用默认设置");
        return;
    }
    uint8_t buf[AS_CFG_BLOB_SIZE];
    size_t len = sizeof buf;
    const esp_err_t ge = nvs_get_blob(h, KEY_CFG, buf, &len);
    nvs_close(h);
    if (ge != ESP_OK) {
        ESP_LOGI(TAG, "尚无设置存档（%s），使用默认设置", esp_err_to_name(ge));
        return;
    }
    if (!as_cfg_unpack(cfg, buf, len)) {
        ESP_LOGW(TAG, "设置存档无效，使用默认设置");
        return;
    }
    ESP_LOGI(TAG, "设置已读取：轨道 %u/%u/%u，层音量 %u/%u/%u，主音量 %u，定时档 %u，呼吸 %u", cfg->sound[0],
             cfg->sound[1], cfg->sound[2], cfg->level[0], cfg->level[1], cfg->level[2], cfg->volume, cfg->timer,
             cfg->breath);
}

void as_store_mark(uint32_t now_ms) {
    s_dirty = true;
    s_changed_ms = now_ms;
}

// 写入设置 blob 并提交；失败时保留脏标记，稍后重试。
static void save_now(const as_cfg_t *cfg, uint32_t now_ms) {
    nvs_handle_t h;
    esp_err_t e = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        uint8_t buf[AS_CFG_BLOB_SIZE];
        const size_t n = as_cfg_pack(cfg, buf, sizeof buf);
        e = nvs_set_blob(h, KEY_CFG, buf, n);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e == ESP_OK) {
        s_dirty = false;
        ESP_LOGI(TAG, "设置已保存");
    } else {
        ESP_LOGW(TAG, "保存失败（%s），稍后重试", esp_err_to_name(e));
        s_changed_ms = now_ms;
    }
}

void as_store_tick(const as_cfg_t *cfg, uint32_t now_ms, bool quiet) {
    if (!s_ok || !s_dirty || !quiet || now_ms - s_changed_ms < SAVE_DELAY_MS) return;
    save_now(cfg, now_ms);
}

void as_store_flush(const as_cfg_t *cfg) {
    if (!s_ok || !s_dirty) return;
    save_now(cfg, s_changed_ms);
}
