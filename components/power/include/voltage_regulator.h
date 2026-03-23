#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c.h"

typedef struct {
    i2c_port_t port;
    uint8_t    address;
} vr_config_t;

typedef struct {
    float vin;
    float vout;
    float iin;
    float iout;
    float pin;
    float pout;
    float temperature;
} vr_telemetry_t;

esp_err_t vr_init(const vr_config_t *config);
esp_err_t vr_set_voltage(uint16_t millivolts);
esp_err_t vr_enable(bool enable);
esp_err_t vr_read_telemetry(vr_telemetry_t *telemetry);
bool      vr_check_faults(void);
