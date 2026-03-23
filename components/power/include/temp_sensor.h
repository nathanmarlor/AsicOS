#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"

#define TEMP_SENSOR_MAX_COUNT 4

/* Temperature sensor types */
#define TEMP_TYPE_TMP1075   0
#define TEMP_TYPE_EMC2101   1

typedef struct {
    i2c_port_t port;
    uint8_t    addresses[TEMP_SENSOR_MAX_COUNT];
    uint8_t    count;
    uint8_t    type;          /* TEMP_TYPE_TMP1075 or TEMP_TYPE_EMC2101 */
    uint8_t    mux_addr;      /* PAC9544 mux address (used when type == EMC2101) */
    uint8_t    mux_channels[TEMP_SENSOR_MAX_COUNT]; /* mux channel per sensor */
} temp_sensor_config_t;

esp_err_t temp_sensor_init(const temp_sensor_config_t *config);
esp_err_t temp_sensor_read(uint8_t index, float *temperature);
esp_err_t temp_sensor_read_max(float *temperature);
