#include "bm1370.h"
#include "asic.h"
#include "asic_packet.h"
#include "serial.h"

#include <string.h>

#include "esp_log.h"

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

    /* Assign addresses to each chip */
    for (int i = 0; i < found; i++) {
        uint8_t addr = (uint8_t)(i * 2);
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

    ESP_LOGI(TAG, "BM1370 init complete: %d chip(s) configured", found);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Set frequency                                                       */
/* ------------------------------------------------------------------ */
esp_err_t bm1370_set_frequency(uint16_t freq_mhz)
{
    pll_params_t pll = asic_calc_pll((float)freq_mhz);

    ESP_LOGI(TAG, "Setting frequency: target=%u MHz, actual=%.1f MHz "
             "(fb=%u, pd1=%u, pd2=%u)",
             freq_mhz, pll.actual_freq,
             pll.fb_divider, pll.postdiv1, pll.postdiv2);

    /* Build 4-byte PLL register data:
     * byte 0: fb_divider low byte
     * byte 1: (refdiv << 4) | postdiv1
     * byte 2: postdiv2
     * byte 3: 0 (reserved)
     */
    uint8_t data[4];
    data[0] = (uint8_t)(pll.fb_divider & 0xFF);
    data[1] = (uint8_t)((pll.refdiv << 4) | (pll.postdiv1 & 0x0F));
    data[2] = pll.postdiv2;
    data[3] = 0x00;

    uint8_t cmd_buf[16];
    int cmd_len = asic_build_cmd(cmd_buf, sizeof(cmd_buf),
                                 ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                                 ASIC_REG_PLL,
                                 data, sizeof(data));
    if (cmd_len < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    serial_tx(cmd_buf, (size_t)cmd_len);
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
                                 ASIC_REG_TEMPERATURE,
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
