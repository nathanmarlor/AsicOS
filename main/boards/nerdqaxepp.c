/*
 * Board definition: NerdQAxe++
 *
 * Hardware: ESP32-S3 + 4x BM1370
 *
 * Pin sources (confirmed from ESP-Miner-NerdQAxePlus develop branch):
 *   UART TX/RX  – components/bm1397/serial.cpp  (GPIO 17 / 18)
 *   I2C SDA/SCL – main/boards/drivers/i2c_master.cpp (GPIO 44 / 43)
 *   ASIC reset  – main/boards/nerdqaxeplus.cpp  BM1368_RST_PIN (GPIO 1)
 *   Buck enable – main/boards/nerdqaxeplus.cpp  TPS53647_EN_PIN (GPIO 10)
 *   LDO enable  – main/boards/nerdqaxeplus.cpp  LDO_EN_PIN (GPIO 13)
 *   LED pin     – estimated (GPIO 2, same as AsicOS V1; not found in source)
 *
 * Frequency / voltage / thermal values taken from nerdqaxeplus2.cpp constructor.
 */

#include "board.h"

static const board_config_t s_board = {
    .name = "NerdQAxe++",
    .asic_model = "BM1370",

    .uart_tx_pin = 17,   /* confirmed */
    .uart_rx_pin = 18,   /* confirmed */

    .i2c_sda_pin = 44,   /* confirmed – note: different from AsicOS V1 */
    .i2c_scl_pin = 43,   /* confirmed */

    .asic_reset_pin  = 1,   /* confirmed */
    .buck_enable_pin = 10,  /* confirmed (TPS53647 EN) */
    .ldo_enable_pin  = 13,  /* confirmed */
    .led_pin         = 2,   /* estimated – update when hardware verified */

    .freq_default    = 600,
    .freq_min        = 200,
    .freq_max        = 800,
    .voltage_default = 1150,
    .voltage_min     = 1000,
    .voltage_max     = 1400,

    .fan_target_temp = 55,
    .overheat_temp   = 70,
    .vr_target_temp  = 65,

    .job_interval_ms = 500,

    .expected_chip_count = 4,
    .small_core_count    = 2040,
};

const board_config_t *board_get_config(void)
{
    return &s_board;
}
