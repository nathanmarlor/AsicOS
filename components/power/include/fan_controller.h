#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

typedef struct {
    i2c_port_t port;
    uint8_t    address;
} fan_config_t;

esp_err_t fan_init(const fan_config_t *config);
esp_err_t fan_set_speed(uint8_t channel, uint8_t percent);
esp_err_t fan_get_rpm(uint8_t channel, uint16_t *rpm);
