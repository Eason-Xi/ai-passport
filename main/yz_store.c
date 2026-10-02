// main/yz_store.c —— NVS 读写，格式见 yz_save.h。
#include "yz_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "yz_save.h"

static const char *TAG = "yz_store";

#define NAMESPACE "yzcopy"
#define KEY_CFG "cfg"
#define KEY_PROG "prog"
#define SAVE_DELAY_MS 3000

static bool s_ok;
static bool s_cfg_dirty;
static bool s_prog_dirty;
static uint32_t s_changed_ms;
static uint8_t s_buf[YZ_PROG_BLOB_SIZE];   // 只在应用任务里使用

void yz_store_init(yz_cfg_t *cfg, yz_progress_t *prog) {
    yz_cfg_default(cfg);
    yz_progress_reset(prog);

    esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) {
        // 不自动擦除：分区里可能有别的数据，宁可本次不保存。
        ESP_LOGE(TAG, "nvs_flash_init 失败（%s），本次运行不保存进度与设置", esp_err_to_name(e));
        return;
    }
    s_ok = true;

    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "尚无存档，使用默认设置");
        return;
    }
    size_t len = sizeof(s_buf);
    if (nvs_get_blob(h, KEY_CFG, s_buf, &len) == ESP_OK && !yz_cfg_unpack(cfg, s_buf, len)) {
        ESP_LOGW(TAG, "设置存档无效，使用默认值");
        yz_cfg_default(cfg);
    }
    len = sizeof(s_buf);
    if (nvs_get_blob(h, KEY_PROG, s_buf, &len) == ESP_OK && !yz_prog_unpack(prog, s_buf, len)) {
        ESP_LOGW(TAG, "进度存档无效，从头开始");
        yz_progress_reset(prog);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "存档已读取：当前第 %u 字，累计 %lu 遍", (unsigned)prog->current + 1,
             (unsigned long)prog->sessions);
}

void yz_store_mark(bool cfg_dirty, bool prog_dirty, uint32_t now_ms) {
    if (!cfg_dirty && !prog_dirty) return;
    s_cfg_dirty |= cfg_dirty;
    s_prog_dirty |= prog_dirty;
    s_changed_ms = now_ms;
}

void yz_store_tick(const yz_cfg_t *cfg, const yz_progress_t *prog, uint32_t now_ms, bool busy) {
    if (!s_ok || (!s_cfg_dirty && !s_prog_dirty)) return;
    if (busy || now_ms - s_changed_ms < SAVE_DELAY_MS) return;

    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open 失败，稍后重试");
        s_changed_ms = now_ms;
        return;
    }
    esp_err_t e = ESP_OK;
    if (s_cfg_dirty) {
        const size_t n = yz_cfg_pack(cfg, s_buf, sizeof(s_buf));
        e = nvs_set_blob(h, KEY_CFG, s_buf, n);
    }
    if (e == ESP_OK && s_prog_dirty) {
        const size_t n = yz_prog_pack(prog, s_buf, sizeof(s_buf));
        e = nvs_set_blob(h, KEY_PROG, s_buf, n);
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) {
        s_cfg_dirty = false;
        s_prog_dirty = false;
    } else {
        ESP_LOGW(TAG, "保存失败（%s），稍后重试", esp_err_to_name(e));
        s_changed_ms = now_ms;
    }
}
