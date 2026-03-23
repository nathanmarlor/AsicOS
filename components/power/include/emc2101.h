#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

typedef struct {
    i2c_port_t port;
    uint8_t mux_addr;     // PAC9544 address (0x70)
    uint8_t mux_channel;  // 2 or 3
    uint8_t dev_addr;     // 0x4C
} emc2101_config_t;

esp_err_t emc2101_init(const emc2101_config_t *config);
esp_err_t emc2101_read_external_temp(const emc2101_config_t *config, float *temp);
esp_err_t emc2101_read_internal_temp(const emc2101_config_t *config, float *temp);
esp_err_t emc2101_get_fan_rpm(const emc2101_config_t *config, uint16_t *rpm);
esp_err_t emc2101_set_fan_duty(const emc2101_config_t *config, uint8_t percent);
