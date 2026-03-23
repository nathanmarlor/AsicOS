#include "board.h"

// NerdQAxe++ board definition
// Pins confirmed from https://github.com/shufps/ESP-Miner-NerdQAxePlus develop branch

static const board_config_t s_board = {
    .name = "NerdQAxe++",
    .asic_model = "BM1370",

    .uart_tx_pin = 17,
    .uart_rx_pin = 18,

    .i2c_sda_pin = 44,   // Confirmed from i2c_master.cpp
    .i2c_scl_pin = 43,   // Confirmed from i2c_master.cpp

    .asic_reset_pin = 1,   // BM1368_RST_PIN
    .buck_enable_pin = 10, // TPS53647_EN_PIN
    .ldo_enable_pin = 13,  // LDO_EN_PIN
    .led_pin = -1,         // Not defined in source

    .freq_default = 600,
    .freq_min = 500,
    .freq_max = 800,
    .voltage_default = 1150,
    .voltage_min = 1005,
    .voltage_max = 1400,

    .fan_target_temp = 55,
    .overheat_temp = 70,
    .vr_target_temp = 65,

    .job_interval_ms = 500,

    .expected_chip_count = 4,
    .small_core_count = 2040,

    .vr_type = 0,            // TPS53647
    .vr_i2c_addr = 0x71,
    .fan_type = 0,           // EMC2302
    .fan_i2c_addr = 0x2E,
    .temp_type = 0,          // TMP1075
    .temp_sensor_count = 3,
    .power_monitor_type = 0, // VR telemetry
    .power_monitor_addr = 0,

    // LEDs and plug sense
    .led1_gpio = -1,
    .led2_gpio = -1,
    .plug_sense_gpio = -1,
    .has_adc_vcore = false,

    // Display: ST7789 320x170 via Intel 8080 parallel bus
    .has_display = true,
    .display_data_gpio = {39, 40, 41, 42, 45, 46, 47, 48},
    .display_wr_gpio = 8,
    .display_rd_gpio = 9,
    .display_cs_gpio = 6,
    .display_dc_gpio = 7,
    .display_rst_gpio = 5,
    .display_bl_gpio = 38,
    .display_pwr_gpio = 15,
    .button1_gpio = 14,
    .button2_gpio = 0,
    .display_width = 320,
    .display_height = 170,
};

const board_config_t *board_get_config(void)
{
    return &s_board;
}
