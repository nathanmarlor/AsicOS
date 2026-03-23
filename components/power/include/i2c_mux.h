#pragma once
#include "esp_err.h"
#include "driver/i2c.h"

esp_err_t i2c_mux_init(i2c_port_t port, uint8_t mux_addr);
esp_err_t i2c_mux_select(i2c_port_t port, uint8_t mux_addr, uint8_t channel);
