/*
 * Board definition: BitForge Nano
 *
 * Hardware: ESP32-S3 + 2x BM1370  (WantClueTechnologies design)
 *
 * Pin sources (from KiCad schematics in WantClueTechnologies/BitForge-Nano):
 *   UART TX/RX  – ESP32.kicad_sch  GPIO 43 (U0TXD) / GPIO 44 (U0RXD)
 *   I2C SDA/SCL – ESP32.kicad_sch  GPIO 19 / GPIO 20 (with 4.7 kΩ pull-ups)
 *   LED1        – ESP32.kicad_sch  GPIO 8 (confirmed)
 *   PGOOD       – ESP32.kicad_sch  GPIO 5 (confirmed)
 *
 *   ASIC reset  – estimated GPIO 0 (BOOT pin doubles as RST in schematic)
 *   Buck enable – estimated GPIO 5 (PGOOD; actual enable line not identified
 *                 in schematic – update when firmware reference is available)
 *   LDO enable  – estimated GPIO 6 (SMB_ALRT repurposed estimate – update)
 *
 * The board uses a TPS546A24 buck converter and INA260 power monitor with
 * EMC2101 fan controllers, per the project README.
 *
 * Frequency / voltage / thermal values are reasonable BM1370 defaults;
 * update once manufacturer firmware or datasheet becomes available.
 */

#include "board.h"

static const board_config_t s_board = {
    .name = "BitForge Nano",
    .asic_model = "BM1370",

    .uart_tx_pin = 43,   /* confirmed from schematic */
    .uart_rx_pin = 44,   /* confirmed from schematic */

    .i2c_sda_pin = 19,   /* confirmed from schematic */
    .i2c_scl_pin = 20,   /* confirmed from schematic */

    .asic_reset_pin  = 0,   /* estimated – needs hardware verification */
    .buck_enable_pin = 5,   /* estimated – needs hardware verification */
    .ldo_enable_pin  = 6,   /* estimated – needs hardware verification */
    .led_pin         = 8,   /* confirmed (LED1) */

    .freq_default    = 490,
    .freq_min        = 200,
    .freq_max        = 800,
    .voltage_default = 1200,
    .voltage_min     = 1000,
    .voltage_max     = 1400,

    .fan_target_temp = 55,
    .overheat_temp   = 70,
    .vr_target_temp  = 65,

    .job_interval_ms = 1200,

    .expected_chip_count = 2,
    .small_core_count    = 2040,
};

const board_config_t *board_get_config(void)
{
    return &s_board;
}
