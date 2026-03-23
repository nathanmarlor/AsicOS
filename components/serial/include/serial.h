#pragma once

#include <stdint.h>
#include <stddef.h>
#include "driver/uart.h"

typedef struct {
    uart_port_t port;
    int tx_pin;
    int rx_pin;
    int baud_rate;
} serial_config_t;

esp_err_t serial_init(const serial_config_t *config);
int serial_tx(const uint8_t *data, size_t len);
int serial_rx(uint8_t *buf, size_t len, uint32_t timeout_ms);
esp_err_t serial_set_baud(int baud_rate);
void serial_flush_rx(void);
