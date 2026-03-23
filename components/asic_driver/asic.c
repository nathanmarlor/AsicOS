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

    for (uint16_t fb = 0xA0; fb <= 0xEF; fb++) {
        for (uint8_t pd1 = 1; pd1 <= 7; pd1++) {
            for (uint8_t pd2 = 1; pd2 <= 7; pd2++) {
                float freq = 25.0f * (float)fb / (1.0f * (float)pd2 * (float)pd1);
                float err  = fabsf(freq - target_freq_mhz);
                if (err < best_err) {
                    best_err          = err;
                    best.fb_divider   = fb;
                    best.postdiv1     = pd1;
                    best.postdiv2     = pd2;
                    best.actual_freq  = freq;
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
                                 ASIC_REG_VR_MASK, ver_data, 4);
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
                                 ASIC_REG_CHIP_ID,
                                 NULL, 0);
    if (cmd_len < 0) {
        ESP_LOGE(TAG, "Failed to build enumerate command");
        return 0;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);

    /* Read responses – each chip replies with ASIC_RESP_SIZE bytes */
    int chip_count = 0;
    uint8_t resp[ASIC_RESP_SIZE];

    while (1) {
        int n = serial_rx(resp, ASIC_RESP_SIZE, 500);
        if (n != ASIC_RESP_SIZE) {
            break;
        }

        /* Validate BM1370 signature: 0xAA 0x55 0x13 0x70 */
        if (resp[0] == 0xAA && resp[1] == 0x55 &&
            resp[2] == 0x13 && resp[3] == 0x70) {
            chip_count++;
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
    /* SETADDR payload: chip_index-derived address byte + target address */
    uint8_t data[2];
    data[0] = (uint8_t)(chip_index * 2);  /* chip_index encoded */
    data[1] = addr;

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_SETADDR, ASIC_GROUP_SINGLE,
                                 ASIC_REG_CHIP_ID,
                                 data, sizeof(data));
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
    /* Compute largest power-of-two <= difficulty */
    if (difficulty == 0) {
        difficulty = 1;
    }
    uint64_t mask = 1;
    while ((mask << 1) <= difficulty) {
        mask <<= 1;
    }
    mask -= 1;  /* e.g. difficulty 256 -> mask 0xFF */

    /* Build 8-byte data with bit-reversed bytes */
    uint8_t data[8];
    for (int i = 0; i < 8; i++) {
        data[i] = bit_reverse_byte((uint8_t)((mask >> (56 - i * 8)) & 0xFF));
    }

    uint8_t cmd_buf[24];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                                 ASIC_REG_TICKET_MASK,
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
    uint8_t data[4];
    data[0] = (uint8_t)((mask >> 24) & 0xFF);
    data[1] = (uint8_t)((mask >> 16) & 0xFF);
    data[2] = (uint8_t)((mask >>  8) & 0xFF);
    data[3] = (uint8_t)((mask >>  0) & 0xFF);

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                                 ASIC_REG_VR_MASK,
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
    if (n != ASIC_RESP_SIZE) {
        return -1;
    }

    /* Validate preamble */
    if (resp[0] != 0xAA || resp[1] != 0x55) {
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
    uint8_t data[2];
    data[0] = chip_addr;
    data[1] = 0x00;

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_READ, ASIC_GROUP_SINGLE,
                                 ASIC_REG_NONCE_COUNT,
                                 data, sizeof(data));
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
