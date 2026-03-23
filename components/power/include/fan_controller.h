#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

/* Fan controller types */
#define FAN_TYPE_EMC2302      0
#define FAN_TYPE_EMC2101_MUX  1

typedef struct {
    i2c_port_t port;
    uint8_t    address;    /* EMC2302 addr or PAC9544 mux addr */
    uint8_t    type;       /* FAN_TYPE_EMC2302 or FAN_TYPE_EMC2101_MUX */
    uint8_t    mux_channels[2]; /* mux channels for EMC2101 (e.g. 2, 3) */
    uint8_t    dev_addr;   /* EMC2101 device addr behind mux (0x4C) */
} fan_config_t;

esp_err_t fan_init(const fan_config_t *config);
esp_err_t fan_set_speed(uint8_t channel, uint8_t percent);
esp_err_t fan_get_rpm(uint8_t channel, uint16_t *rpm);
