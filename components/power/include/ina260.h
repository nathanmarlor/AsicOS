#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

typedef struct {
    i2c_port_t port;
    uint8_t address;  // 0x40
} ina260_config_t;

typedef struct {
    float voltage_mv;
    float current_ma;
    float power_mw;
} ina260_reading_t;

esp_err_t ina260_init(const ina260_config_t *config);
esp_err_t ina260_read(const ina260_config_t *config, ina260_reading_t *reading);
