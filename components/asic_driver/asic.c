#include "asic.h"
#include "asic_packet.h"
#include "serial.h"

#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "lwip/inet.h"  /* ntohl */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "asic";

static asic_state_t s_state = {0};

/* ------------------------------------------------------------------ */
/* Hash counter relay (result_task -> hashrate_task)                    */
/* ------------------------------------------------------------------ */
static volatile uint32_t s_last_hash_counter[16] = {0};
static volatile bool     s_hash_counter_valid[16] = {false};

/* ------------------------------------------------------------------ */
/* Domain counter relay (per-domain hash counts, 4 domains per chip)   */
/* ------------------------------------------------------------------ */
static volatile uint32_t s_domain_counters[16][ASIC_NUM_DOMAINS] = {{0}};
static volatile bool     s_domain_counter_valid[16][ASIC_NUM_DOMAINS] = {{false}};

void asic_store_hash_counter(int chip, uint32_t value)
{
    if (chip >= 0 && chip < 16) {
        s_last_hash_counter[chip] = value;
        s_hash_counter_valid[chip] = true;
    }
}

uint32_t asic_get_stored_hash_counter(int chip)
{
    if (chip >= 0 && chip < 16 && s_hash_counter_valid[chip]) {
        s_hash_counter_valid[chip] = false;
        return s_last_hash_counter[chip];
    }
    return 0;
}

void asic_store_domain_counter(int chip, int domain, uint32_t value)
{
    if (chip >= 0 && chip < 16 && domain >= 0 && domain < ASIC_NUM_DOMAINS) {
        s_domain_counters[chip][domain] = value;
        s_domain_counter_valid[chip][domain] = true;
    }
}

uint32_t asic_get_stored_domain_counter(int chip, int domain)
{
    if (chip >= 0 && chip < 16 && domain >= 0 && domain < ASIC_NUM_DOMAINS
        && s_domain_counter_valid[chip][domain]) {
        s_domain_counter_valid[chip][domain] = false;
        return s_domain_counters[chip][domain];
    }
    return 0;
}

void asic_request_domain_counters(uint8_t chip_addr)
{
    /* Request registers 0x88..0x8B (4 domains) */
    static const uint8_t domain_regs[ASIC_NUM_DOMAINS] = {
        ASIC_REG_DOMAIN_0, ASIC_REG_DOMAIN_1,
        ASIC_REG_DOMAIN_2, ASIC_REG_DOMAIN_3
    };
    for (int d = 0; d < ASIC_NUM_DOMAINS; d++) {
        uint8_t cmd_buf[16];
        int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                     ASIC_CMD_READ, ASIC_GROUP_SINGLE,
                                     chip_addr, domain_regs[d],
                                     NULL, 0);
        if (cmd_len > 0) {
            serial_tx(cmd_buf, (size_t)cmd_len);
        }
    }
}

void asic_request_hash_counter(uint8_t chip_addr)
{
    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_READ, ASIC_GROUP_SINGLE,
                                 chip_addr, ASIC_REG_NONCE_COUNT,
                                 NULL, 0);
    if (cmd_len > 0) {
        serial_tx(cmd_buf, (size_t)cmd_len);
    }
}

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

    /* Search: refdiv 2..1, postdiv1 7..1, postdiv2 7..1,
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
     * Send the actual version mask value ( _send_init). */
    uint32_t default_mask = 0x1FFFE000;
    uint32_t versions_to_roll = default_mask >> 13;
    uint8_t ver_data[4] = {0x90, 0x00,
                           (uint8_t)((versions_to_roll >> 8) & 0xFF),
                           (uint8_t)(versions_to_roll & 0xFF)};
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

    /* Read all chip responses. At 115200 baud, each 11-byte response takes ~1ms.
     * Read byte-by-byte with short timeouts to catch all responses without missing
     * any that arrive back-to-back. */
    uint8_t bulk[128];
    int total_read = 0;

    /* Keep reading until we get no data for 500ms */
    while (total_read < (int)sizeof(bulk)) {
        uint8_t byte;
        int n = serial_rx(&byte, 1, 500);
        if (n != 1) break;
        bulk[total_read++] = byte;
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
     * : difficulty = _largest_power_of_two(difficulty) - 1 */
    if (difficulty == 0) {
        difficulty = 1;
    }
    uint64_t pot = 1;
    while ((pot << 1) <= difficulty) {
        pot <<= 1;
    }
    uint32_t mask = (uint32_t)(pot - 1);  /* e.g. difficulty 256 -> mask 0xFF */

    /* Build 4-byte data with bit-reversed bytes ( format).
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
        /* Parse nonce response  packed struct layout:
         * [2..5] nonce - stored as-is via memcpy (LE uint32 on ESP32)
         * [6]    midstate_num
         * [7]    job_id
         * [8..9] rolled_version - stored as-is via memcpy (LE uint16 on ESP32)
         *
         * Forge-os uses ntohl/ntohs only when extracting specific bit fields
         * (core_id, version_bits), but stores raw LE values in the result struct.
         */
        memcpy(&result->nonce, &resp[2], 4);  /* raw bytes, LE on ESP32 */
        result->midstate_num   = resp[6];
        result->job_id         = resp[7];
        memcpy(&result->rolled_version, &resp[8], 2);  /* raw bytes, LE on ESP32 */
        return 1;
    }

    /* Register read response -  bm1370_asic_result_cmd_t:
     * [2..5] value (uint32_t, big-endian - use ntohl)
     * [6]    asic_address (chip address: 0x00, 0x04, etc)
     * [7]    register_address (0x8C for hash count, 0xB4 for temp, etc)
     * [8..9] unused
     * [10]   crc (bit 7 = 0 for register read) */
    uint32_t raw_value;
    memcpy(&raw_value, &resp[2], 4);
    uint32_t value = ntohl(raw_value);
    int chip = resp[6] / 4;  /* asic_address / interval */
    uint8_t reg = resp[7];
    ESP_LOGD(TAG, "Register read: chip=%d reg=0x%02x value=0x%08lx",
             chip, reg, (unsigned long)value);

    /* Route to domain counters or total hash counter */
    if (reg >= ASIC_REG_DOMAIN_0 && reg <= ASIC_REG_DOMAIN_3) {
        int domain = reg - ASIC_REG_DOMAIN_0;
        asic_store_domain_counter(chip, domain, value);
    } else {
        asic_store_hash_counter(chip, value);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* State accessor                                                      */
/* ------------------------------------------------------------------ */
const asic_state_t *asic_get_state(void)
{
    return &s_state;
}
