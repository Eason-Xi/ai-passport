// main/kj_store.h —— NVS 存储：上次的角色、上次入座的赌局号、庄家的赌局快照。
#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

esp_err_t kj_store_init(void);   // nvs_flash_init（不擦除已有数据）
uint8_t kj_store_get_role(void); // 0 选手（默认）/ 1 庄家
void kj_store_set_role(uint8_t role);
uint16_t kj_store_get_room(void);
void kj_store_set_room(uint16_t room);
// 庄家赌局快照
esp_err_t kj_store_save_game(const uint8_t *blob, size_t len);
size_t kj_store_load_game(uint8_t *blob, size_t cap);   // 返回长度，没有或失败返回 0
