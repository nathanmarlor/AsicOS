#include "board.h"

// BitForge Nano board definition
// Pins confirmed from https://github.com/nathanmarlor/forge-os
// Hardware: ESP32-S3, 2x BM1370, TPS546D24A VR, INA260 power monitor,
//           2x EMC2101 fan/temp via PAC9544 I2C mux

static const board_config_t s_board = {
    .name = "BitForge Nano",
    .asic_model = "BM1370",

    .uart_tx_pin = 17,    // Confirmed
    .uart_rx_pin = 18,    // Confirmed

    .i2c_sda_pin = 47,    // CONFIG_GPIO_I2C_SDA
    .i2c_scl_pin = 48,    // CONFIG_GPIO_I2C_SCL

    .asic_reset_pin = 1,   // CONFIG_GPIO_ASIC_RESET
    .buck_enable_pin = 10, // CONFIG_GPIO_ASIC_ENABLE
    .ldo_enable_pin = -1,  // No separate LDO on Nano
    .led_pin = 9,          // BLINK_GPIO_1

    .freq_default = 250,
    .freq_min = 50,
    .freq_max = 800,
    .voltage_default = 1400,
    .voltage_min = 1000,
    .voltage_max = 1800,

    .fan_target_temp = 55,
    .overheat_temp = 75,   // THROTTLE_TEMP from forge-os
    .vr_target_temp = 85,  // VR auto-fan range tops at 85C

    .job_interval_ms = 500,

    .expected_chip_count = 2,
    .small_core_count = 2040,

    .vr_type = 1,            // TPS546D24A
    .vr_i2c_addr = 0x24,
    .fan_type = 1,           // EMC2101 via PAC9544 mux
    .fan_i2c_addr = 0x70,    // PAC9544 mux address
    .temp_type = 1,          // EMC2101 integrated
    .temp_sensor_count = 2,
    .power_monitor_type = 1, // INA260
    .power_monitor_addr = 0x40,
};

const board_config_t *board_get_config(void)
{
    return &s_board;
}
