#pragma once

#include <stdint.h>

typedef struct {
    const char *name;
    const char *asic_model;

    int uart_tx_pin;
    int uart_rx_pin;

    int i2c_sda_pin;
    int i2c_scl_pin;

    int asic_reset_pin;
    int buck_enable_pin;
    int ldo_enable_pin;
    int led_pin;

    uint16_t freq_default;
    uint16_t freq_min;
    uint16_t freq_max;
    uint16_t voltage_default;
    uint16_t voltage_min;
    uint16_t voltage_max;

    uint16_t fan_target_temp;
    uint16_t overheat_temp;
    uint16_t vr_target_temp;

    uint16_t job_interval_ms;

    uint8_t expected_chip_count;
    uint16_t small_core_count;
} board_config_t;

const board_config_t *board_get_config(void);
