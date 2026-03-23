#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Config keys
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_POOL_URL        "pool_url"
#define NVS_KEY_POOL_PORT       "pool_port"
#define NVS_KEY_POOL_USER       "pool_user"
#define NVS_KEY_POOL_PASS       "pool_pass"
#define NVS_KEY_ASIC_FREQ       "asic_freq"
#define NVS_KEY_ASIC_VOLTAGE    "asic_voltage"
#define NVS_KEY_FAN_TARGET_TEMP "fan_target"
#define NVS_KEY_OVERHEAT_TEMP   "overheat"
#define NVS_KEY_BEST_DIFF       "best_diff"
#define NVS_KEY_UI_MODE         "ui_mode"
#define NVS_KEY_LICENCE         "licence"
#define NVS_KEY_DEVICE_ID       "device_id"
#define NVS_KEY_TUNER_PROFILE   "tuner_prof"
#define NVS_KEY_POOL2_URL       "pool2_url"
#define NVS_KEY_POOL2_PORT      "pool2_port"
#define NVS_KEY_POOL2_USER      "pool2_user"

// Defaults
#define DEFAULT_ASIC_FREQ       490
#define DEFAULT_ASIC_VOLTAGE    1250
#define DEFAULT_FAN_TARGET      55
#define DEFAULT_OVERHEAT_TEMP   70
#define DEFAULT_POOL_PORT       3333

esp_err_t nvs_config_init(void);

void nvs_config_get_string(const char *key, char *out, size_t out_len, const char *default_val);
esp_err_t nvs_config_set_string(const char *key, const char *value);

uint16_t nvs_config_get_u16(const char *key, uint16_t default_val);
esp_err_t nvs_config_set_u16(const char *key, uint16_t value);

uint64_t nvs_config_get_u64(const char *key, uint64_t default_val);
esp_err_t nvs_config_set_u64(const char *key, uint64_t value);
