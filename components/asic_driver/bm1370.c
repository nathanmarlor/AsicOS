#include "bm1370.h"
#include "asic.h"
#include "asic_packet.h"
#include "serial.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bm1370";

/* ------------------------------------------------------------------ */
/* Job ID mapping                                                      */
/* ------------------------------------------------------------------ */
uint8_t bm1370_job_to_asic_id(uint8_t job_id)
{
    return (uint8_t)((job_id * 24) & 0x7F);
}

uint8_t bm1370_asic_to_job_id(uint8_t asic_id)
{
    /* Linear search for inverse mapping */
    for (uint8_t jid = 0; jid < BM1370_MAX_JOB_ID; jid++) {
        if (bm1370_job_to_asic_id(jid) == asic_id) {
            return jid;
        }
    }
    return 0xFF;  /* not found */
}

/* ------------------------------------------------------------------ */
/* Nonce to chip                                                       */
/* ------------------------------------------------------------------ */
int bm1370_nonce_to_chip(uint32_t nonce, int chip_count)
{
    if (chip_count <= 0) {
        return 0;
    }
    return (int)((nonce >> 11) & (uint32_t)(chip_count - 1));
}

/* ------------------------------------------------------------------ */
/* Register write helpers                                              */
/* ------------------------------------------------------------------ */

/* Write a register to all chips (broadcast) */
static void write_reg_all(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[24];
    int n = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_WRITE, ASIC_GROUP_ALL, 0x00, reg, data, len);
    if (n > 0) serial_tx(buf, n);
    vTaskDelay(pdMS_TO_TICKS(5));
}

/* Write a register to a specific chip */
static void write_reg_chip(uint8_t chip_addr, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[24];
    int n = asic_build_cmd(buf, sizeof(buf), ASIC_CMD_WRITE, ASIC_GROUP_SINGLE, chip_addr, reg, data, len);
    if (n > 0) serial_tx(buf, n);
    vTaskDelay(pdMS_TO_TICKS(5));
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */
esp_err_t bm1370_init(int expected_chips)
{
    /* Flush stale RX data */
    serial_flush_rx();

    /* Enumerate chips */
    int found = asic_enumerate();
    ESP_LOGI(TAG, "Found %d chip(s) (expected %d)", found, expected_chips);

    if (found == 0) {
        ESP_LOGE(TAG, "No chips found during enumeration");
        return ESP_ERR_NOT_FOUND;
    }

    /* Assign addresses to each chip (forge-os uses i*4 spacing) */
    for (int i = 0; i < found; i++) {
        uint8_t addr = (uint8_t)(i * 4);
        esp_err_t err = asic_set_chip_address(i, addr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set address for chip %d", i);
            return err;
        }
    }

    /* Configure version rolling mask */
    esp_err_t err = asic_set_version_mask(0x1FFFE000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set version mask");
        return err;
    }

    /* ---- Full register init sequence (matching forge-os _send_init) ---- */

    /* 1. Reg 0xA8 - unknown config */
    write_reg_all(0xA8, (uint8_t[]){0x00, 0x07, 0x00, 0x00}, 4);

    /* 2. Misc Control reg 0x18 */
    write_reg_all(ASIC_REG_MISC_CTRL, (uint8_t[]){0xF0, 0x00, 0xC1, 0x00}, 4);

    /* 3. Core Register Control reg 0x3C */
    write_reg_all(ASIC_REG_CORE_CTRL, (uint8_t[]){0x80, 0x00, 0x8B, 0x00}, 4);

    /* 4. Core Register Control reg 0x3C (second write) */
    write_reg_all(ASIC_REG_CORE_CTRL, (uint8_t[]){0x80, 0x00, 0x80, 0x0C}, 4);

    /* 5. Set ticket mask (difficulty 256) */
    asic_set_difficulty_mask(256);

    /* 6. IO Driver Strength reg 0x58 */
    write_reg_all(ASIC_REG_IO_DRIVE, (uint8_t[]){0x00, 0x01, 0x11, 0x11}, 4);

    /* 7. Per-chip register writes */
    for (int i = 0; i < found; i++) {
        uint8_t addr = (uint8_t)(i * 4);
        write_reg_chip(addr, 0xA8, (uint8_t[]){0x00, 0x07, 0x01, 0xF0}, 4);
        write_reg_chip(addr, ASIC_REG_MISC_CTRL, (uint8_t[]){0xF0, 0x00, 0xC1, 0x00}, 4);
        write_reg_chip(addr, ASIC_REG_CORE_CTRL, (uint8_t[]){0x80, 0x00, 0x8B, 0x00}, 4);
        write_reg_chip(addr, ASIC_REG_CORE_CTRL, (uint8_t[]){0x80, 0x00, 0x80, 0x0C}, 4);
        write_reg_chip(addr, ASIC_REG_CORE_CTRL, (uint8_t[]){0x80, 0x00, 0x82, 0xAA}, 4);
    }

    /* 8. Additional config to all */
    write_reg_all(0xB9, (uint8_t[]){0x00, 0x00, 0x44, 0x80}, 4);
    write_reg_all(ASIC_REG_ANALOG_MUX, (uint8_t[]){0x00, 0x00, 0x00, 0x02}, 4);  /* temp diode */
    write_reg_all(0xB9, (uint8_t[]){0x00, 0x00, 0x44, 0x80}, 4);
    write_reg_all(ASIC_REG_CORE_CTRL, (uint8_t[]){0x80, 0x00, 0x8D, 0xEE}, 4);

    /* 9. Frequency ramp is handled by bm1370_set_frequency (ramps from 56.25 MHz) */

    /* 10. Hash counting register reg 0x10 */
    write_reg_all(ASIC_REG_VR_FREQ, (uint8_t[]){0x00, 0x00, 0x1E, 0xB5}, 4);

    ESP_LOGI(TAG, "BM1370 init complete: %d chip(s) configured", found);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Set frequency                                                       */
/* ------------------------------------------------------------------ */
/* Current frequency tracker for ramping (in units of 0.01 MHz) */
static uint16_t s_current_freq_x100 = 0;  /* 0 means unknown / cold start */

/* Write a single PLL step and wait */
static esp_err_t pll_write_step(float freq_mhz)
{
    pll_params_t pll = asic_calc_pll(freq_mhz);

    uint8_t data[4];
    data[0] = (uint8_t)(pll.fb_divider & 0xFF);
    data[1] = (uint8_t)((pll.refdiv << 4) | (pll.postdiv1 & 0x0F));
    data[2] = pll.postdiv2;
    data[3] = 0x00;

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                                 0x00, ASIC_REG_PLL,
                                 data, sizeof(data));
    if (cmd_len < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t bm1370_set_frequency(uint16_t freq_mhz)
{
    const float start_freq = 56.25f;
    const float step       = 6.25f;
    float target           = (float)freq_mhz;

    /* Determine ramp start: use current frequency if known, otherwise 56.25 */
    float current = (s_current_freq_x100 > 0)
                  ? (float)s_current_freq_x100 / 100.0f
                  : start_freq;

    ESP_LOGI(TAG, "Ramping frequency: %.2f -> %.0f MHz (step %.2f MHz)",
             current, target, step);

    if (current < target) {
        /* Ramp up */
        for (float f = current + step; f < target; f += step) {
            esp_err_t rc = pll_write_step(f);
            if (rc != ESP_OK) return rc;
        }
    } else if (current > target) {
        /* Ramp down */
        for (float f = current - step; f > target; f -= step) {
            esp_err_t rc = pll_write_step(f);
            if (rc != ESP_OK) return rc;
        }
    }

    /* Final step to exact target */
    esp_err_t rc = pll_write_step(target);
    if (rc != ESP_OK) return rc;

    s_current_freq_x100 = (uint16_t)(target * 100.0f);

    pll_params_t pll = asic_calc_pll(target);
    ESP_LOGI(TAG, "Frequency set: target=%u MHz, actual=%.1f MHz "
             "(fb=%u, pd1=%u, pd2=%u)",
             freq_mhz, pll.actual_freq,
             pll.fb_divider, pll.postdiv1, pll.postdiv2);

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Read temperature                                                    */
/* ------------------------------------------------------------------ */
float bm1370_read_temperature(void)
{
    /* Build READ command for temperature register, chip 0 */
    uint8_t data[2] = {0x00, 0x00};

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_READ, ASIC_GROUP_SINGLE,
                                 0x00, ASIC_REG_TEMPERATURE,
                                 data, sizeof(data));
    if (cmd_len < 0) {
        return -1.0f;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);

    /* Read response */
    uint8_t resp[ASIC_RESP_SIZE];
    int n = serial_rx(resp, ASIC_RESP_SIZE, 500);
    if (n != ASIC_RESP_SIZE) {
        return -1.0f;
    }

    if (resp[0] != 0xAA || resp[1] != 0x55) {
        return -1.0f;
    }

    /* Parse raw temperature from response bytes [2..3] (16-bit big-endian) */
    uint16_t raw = (uint16_t)((resp[2] << 8) | resp[3]);
    float temp = (float)raw * 0.171342f - 299.5144f;

    return temp;
}

/* ------------------------------------------------------------------ */
/* Send work                                                           */
/* ------------------------------------------------------------------ */

/* Helper: write a big-endian uint32 into buffer */
static void put_be32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >>  8) & 0xFF);
    buf[3] = (uint8_t)((val >>  0) & 0xFF);
}

esp_err_t bm1370_send_work(const asic_job_t *job)
{
    if (!job) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Build 86-byte job data:
     * [0]      asic job_id (mapped)
     * [1]      midstate_count
     * [2..5]   starting_nonce (big-endian)
     * [6..9]   nbits (big-endian)
     * [10..13] ntime (big-endian)
     * [14..45] merkle_root (32 bytes)
     * [46..77] prev_block_hash (32 bytes)
     * [78..81] version (big-endian)
     * [82..85] padding / reserved
     */
    uint8_t job_data[86];
    memset(job_data, 0, sizeof(job_data));

    job_data[0] = bm1370_job_to_asic_id(job->job_id);
    job_data[1] = job->midstate_count;

    put_be32(&job_data[2],  job->starting_nonce);
    put_be32(&job_data[6],  job->nbits);
    put_be32(&job_data[10], job->ntime);

    memcpy(&job_data[14], job->merkle_root, 32);
    memcpy(&job_data[46], job->prev_block_hash, 32);

    put_be32(&job_data[78], job->version);

    /* Wrap in job packet (adds preamble, header, CRC16) */
    uint8_t pkt_buf[128];
    int pkt_len = asic_build_job(pkt_buf, sizeof(pkt_buf),
                                 job_data, sizeof(job_data));
    if (pkt_len < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    serial_tx(pkt_buf, (size_t)pkt_len);
    return ESP_OK;
}
