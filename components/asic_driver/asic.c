#include "asic.h"
#include "asic_packet.h"
#include "serial.h"

#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "asic";

static asic_state_t s_state = {0};

/* ------------------------------------------------------------------ */
/* PLL calculation                                                     */
/* ------------------------------------------------------------------ */
pll_params_t asic_calc_pll(float target_freq_mhz)
{
    pll_params_t best = {
        .refdiv     = 1,
        .fb_divider = 0xA0,
        .postdiv1   = 1,
        .postdiv2   = 1,
        .actual_freq = 0.0f,
    };
    float best_err = 1e9f;
    const float max_diff = 1.0f;

    /* Match forge-os search order: refdiv 2..1, postdiv1 7..1, postdiv2 7..1,
     * with constraint postdiv1 >= postdiv2 */
    for (uint8_t refdiv = 2; refdiv >= 1; refdiv--) {
        for (uint8_t pd1 = 7; pd1 >= 1; pd1--) {
            for (uint8_t pd2 = 7; pd2 >= 1; pd2--) {
                if (pd1 < pd2) continue;
                int fb = (int)roundf((float)(pd1 * pd2) * target_freq_mhz * (float)refdiv / 25.0f);
                if (fb >= 0xA0 && fb <= 0xEF) {
                    float freq = 25.0f * (float)fb / ((float)refdiv * (float)pd2 * (float)pd1);
                    float err = fabsf(target_freq_mhz - freq);
                    if (err < best_err && err < max_diff) {
                        best_err         = err;
                        best.refdiv      = refdiv;
                        best.fb_divider  = (uint16_t)fb;
                        best.postdiv1    = pd1;
                        best.postdiv2    = pd2;
                        best.actual_freq = freq;
                    }
                }
            }
        }
    }

    return best;
}

/* ------------------------------------------------------------------ */
/* Enumerate                                                           */
/* ------------------------------------------------------------------ */
int asic_enumerate(void)
{
    /* Flush any stale data */
    serial_flush_rx();

    /* BM1370 needs version mask writes before it responds to chip ID reads.
     * Send version mask (reg 0xA4) three times as a wake-up sequence. */
    uint8_t ver_data[4] = {0x00, 0x00, 0x00, 0x00};  /* version mask placeholder */
    uint8_t ver_buf[16];
    int ver_len = asic_build_cmd(ver_buf, sizeof(ver_buf),
                                 ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                                 0x00, ASIC_REG_VR_MASK, ver_data, 4);
    if (ver_len > 0) {
        serial_tx(ver_buf, (size_t)ver_len);
        serial_tx(ver_buf, (size_t)ver_len);
        serial_tx(ver_buf, (size_t)ver_len);
    }

    /* Small delay for chips to process */
    vTaskDelay(pdMS_TO_TICKS(10));
    serial_flush_rx();

    /* Broadcast read of CHIP_ID register (reg 0x00, group ALL) */
    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_READ, ASIC_GROUP_ALL,
                                 0x00, ASIC_REG_CHIP_ID,
                                 NULL, 0);
    if (cmd_len < 0) {
        ESP_LOGE(TAG, "Failed to build enumerate command");
        return 0;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);

    /* Wait for all chips to respond (each sends 11 bytes back-to-back) */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Read all available data in one bulk read, then scan for BM1370 signatures.
     * Each response is 11 bytes starting with AA 55 13 70. */
    uint8_t bulk[128];
    int total_read = 0;
    int chunk;

    /* Read whatever is in the UART buffer */
    while (total_read < (int)sizeof(bulk)) {
        chunk = serial_rx(bulk + total_read, sizeof(bulk) - total_read, 100);
        if (chunk <= 0) break;
        total_read += chunk;
    }

    ESP_LOGI(TAG, "Enumerate: read %d bytes from UART", total_read);

    /* Scan for BM1370 chip ID signatures (AA 55 13 70) */
    int chip_count = 0;
    for (int i = 0; i <= total_read - 4; i++) {
        if (bulk[i] == 0xAA && bulk[i + 1] == 0x55 &&
            bulk[i + 2] == 0x13 && bulk[i + 3] == 0x70) {
            chip_count++;
            i += ASIC_RESP_SIZE - 1;  /* skip past this response */
        }
    }

    s_state.chip_count  = chip_count;
    s_state.initialised = (chip_count > 0);
    ESP_LOGI(TAG, "Enumerated %d chip(s)", chip_count);

    return chip_count;
}

/* ------------------------------------------------------------------ */
/* Set chip address                                                    */
/* ------------------------------------------------------------------ */
esp_err_t asic_set_chip_address(int chip_index, uint8_t addr)
{
    /* Forge-os format: _send_BM1370(TYPE_CMD | GROUP_SINGLE | CMD_SETADDRESS,
     * {chipAddr, 0x00}, 2). The data IS the chip address + padding byte.
     * asic_build_cmd uses chip_addr and reg_addr as the first 2 bytes after
     * header+length, so pass addr as chip_addr and 0x00 as reg, with no extra data. */
    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_SETADDR, ASIC_GROUP_SINGLE,
                                 addr, 0x00,
                                 NULL, 0);
    if (cmd_len < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Difficulty mask                                                     */
/* ------------------------------------------------------------------ */
static uint8_t bit_reverse_byte(uint8_t b)
{
    b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}

esp_err_t asic_set_difficulty_mask(uint64_t difficulty)
{
    /* Compute largest power-of-two <= difficulty, then subtract 1 for mask.
     * Matches forge-os: difficulty = _largest_power_of_two(difficulty) - 1 */
    if (difficulty == 0) {
        difficulty = 1;
    }
    uint64_t pot = 1;
    while ((pot << 1) <= difficulty) {
        pot <<= 1;
    }
    uint32_t mask = (uint32_t)(pot - 1);  /* e.g. difficulty 256 -> mask 0xFF */

    /* Build 4-byte data with bit-reversed bytes (matching forge-os format).
     * The bytes are stored in reverse order: LSB first in the register. */
    uint8_t data[4];
    for (int i = 0; i < 4; i++) {
        data[3 - i] = bit_reverse_byte((uint8_t)((mask >> (i * 8)) & 0xFF));
    }

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                                 0x00, ASIC_REG_TICKET_MASK,
                                 data, sizeof(data));
    if (cmd_len < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Version mask                                                        */
/* ------------------------------------------------------------------ */
esp_err_t asic_set_version_mask(uint32_t mask)
{
    /* Forge-os format: shift mask right by 13 to get the number of versions
     * to roll, then encode as {0x90, 0x00, hi_byte, lo_byte} */
    uint32_t versions_to_roll = mask >> 13;
    uint8_t data[4];
    data[0] = 0x90;
    data[1] = 0x00;
    data[2] = (uint8_t)((versions_to_roll >> 8) & 0xFF);
    data[3] = (uint8_t)((versions_to_roll >> 0) & 0xFF);

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                                 0x00, ASIC_REG_VR_MASK,
                                 data, sizeof(data));
    if (cmd_len < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Receive result                                                      */
/* ------------------------------------------------------------------ */
int asic_receive_result(asic_result_t *result, uint32_t timeout_ms)
{
    if (!result) {
        return -1;
    }

    uint8_t resp[ASIC_RESP_SIZE];
    int n = serial_rx(resp, ASIC_RESP_SIZE, timeout_ms);
    if (n > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, resp, n, ESP_LOG_INFO);
    }
    if (n != ASIC_RESP_SIZE) {
        if (n > 0) {
            ESP_LOGW(TAG, "Partial ASIC response: got %d bytes, expected %d", n, ASIC_RESP_SIZE);
        }
        return -1;
    }

    /* Validate preamble */
    if (resp[0] != 0xAA || resp[1] != 0x55) {
        ESP_LOGW(TAG, "Bad preamble: %02x %02x", resp[0], resp[1]);
        return -1;
    }

    if (asic_is_nonce_response(resp)) {
        /* Parse nonce response:
         * [2..5] nonce (big-endian)
         * [6]    midstate_num
         * [7]    job_id
         * [8..9] rolled_version (big-endian)
         */
        result->nonce = ((uint32_t)resp[2] << 24) |
                        ((uint32_t)resp[3] << 16) |
                        ((uint32_t)resp[4] <<  8) |
                        ((uint32_t)resp[5]);
        result->midstate_num   = resp[6];
        result->job_id         = resp[7];
        result->rolled_version = (uint16_t)((resp[8] << 8) | resp[9]);
        return 1;
    }

    /* Register read response */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Read hash counter                                                   */
/* ------------------------------------------------------------------ */
uint32_t asic_read_hash_counter(uint8_t chip_addr)
{
    /* Build READ command for NONCE_COUNT register targeting specific chip */
    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_READ, ASIC_GROUP_SINGLE,
                                 chip_addr, ASIC_REG_NONCE_COUNT,
                                 NULL, 0);
    if (cmd_len < 0) {
        return 0;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);

    /* Read response */
    uint8_t resp[ASIC_RESP_SIZE];
    int n = serial_rx(resp, ASIC_RESP_SIZE, 500);
    if (n != ASIC_RESP_SIZE) {
        return 0;
    }

    if (resp[0] != 0xAA || resp[1] != 0x55) {
        return 0;
    }

    /* Parse 32-bit counter from response bytes [2..5] */
    uint32_t counter = ((uint32_t)resp[2] << 24) |
                       ((uint32_t)resp[3] << 16) |
                       ((uint32_t)resp[4] <<  8) |
                       ((uint32_t)resp[5]);
    return counter;
}

/* ------------------------------------------------------------------ */
/* State accessor                                                      */
/* ------------------------------------------------------------------ */
const asic_state_t *asic_get_state(void)
{
    return &s_state;
}
