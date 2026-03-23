#pragma once

#include <stdbool.h>
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

    // Voltage regulator type and address
    uint8_t vr_type;        // 0 = TPS53647, 1 = TPS546D24A
    uint8_t vr_i2c_addr;    // I2C address

    // Fan controller type
    uint8_t fan_type;       // 0 = EMC2302, 1 = EMC2101_MUX (2x via PAC9544)
    uint8_t fan_i2c_addr;   // EMC2302 addr or PAC9544 mux addr

    // Temperature sensor type
    uint8_t temp_type;      // 0 = TMP1075, 1 = EMC2101 (integrated)
    uint8_t temp_sensor_count;

    // Power monitor
    uint8_t power_monitor_type;  // 0 = VR_TELEMETRY (from VR PMBus), 1 = INA260
    uint8_t power_monitor_addr;  // INA260 address (0x40) or 0 for VR telemetry

    // LEDs and plug sense
    int led1_gpio;
    int led2_gpio;
    int plug_sense_gpio;
    bool has_adc_vcore;

    // Display
    bool has_display;
    int display_data_gpio[8];
    int display_wr_gpio;
    int display_rd_gpio;
    int display_cs_gpio;
    int display_dc_gpio;
    int display_rst_gpio;
    int display_bl_gpio;
    int display_pwr_gpio;
    int button1_gpio;
    int button2_gpio;
    uint16_t display_width;
    uint16_t display_height;
} board_config_t;

const board_config_t *board_get_config(void);
