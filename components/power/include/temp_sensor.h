#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

#define TEMP_SENSOR_MAX_COUNT 4

typedef struct {
    i2c_port_t port;
    uint8_t    addresses[TEMP_SENSOR_MAX_COUNT];
    uint8_t    count;
} temp_sensor_config_t;

esp_err_t temp_sensor_init(const temp_sensor_config_t *config);
esp_err_t temp_sensor_read(uint8_t index, float *temperature);
esp_err_t temp_sensor_read_max(float *temperature);
