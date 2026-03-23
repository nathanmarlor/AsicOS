#include "board.h"

static const board_config_t s_board = {
    .name = "AsicOS V1",
    .asic_model = "BM1370",

    .uart_tx_pin = 17,
    .uart_rx_pin = 18,

    .i2c_sda_pin = 47,
    .i2c_scl_pin = 48,

    .asic_reset_pin = 1,
    .buck_enable_pin = 10,
    .ldo_enable_pin = 11,
    .led_pin = 2,

    .freq_default = 490,
    .freq_min = 200,
    .freq_max = 800,
    .voltage_default = 1250,
    .voltage_min = 1000,
    .voltage_max = 1400,

    .fan_target_temp = 55,
    .overheat_temp = 70,
    .vr_target_temp = 65,

    .job_interval_ms = 1200,

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
};

const board_config_t *board_get_config(void)
{
    return &s_board;
}
