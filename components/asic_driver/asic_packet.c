#include "asic_packet.h"
#include <string.h>

uint8_t asic_crc5(const uint8_t *data, size_t len)
{
    /* CRC-5/USB: polynomial x^5 + x^2 + 1, init 0x1F
     * Matches BM1370 ASIC protocol (verified against forge-os) */
    uint8_t crc[5] = {1, 1, 1, 1, 1};
    size_t bits = len * 8;

    for (size_t i = 0; i < bits; i++) {
        uint8_t din = (data[i >> 3] >> (7 - (i & 7))) & 1;
        uint8_t d0 = crc[4] ^ din;
        crc[4] = crc[3];
        crc[3] = crc[2];
        crc[2] = crc[1] ^ d0;
        crc[1] = crc[0];
        crc[0] = d0;
    }

    return (uint8_t)((crc[4] << 4) | (crc[3] << 3) | (crc[2] << 2) | (crc[1] << 1) | crc[0]);
}

uint16_t asic_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }

    return crc;
}

int asic_build_cmd(uint8_t *buf, size_t buf_len,
                   uint8_t cmd, uint8_t group,
                   uint8_t chip_addr, uint8_t reg_addr,
                   const uint8_t *data, size_t data_len)
{
    /* BM1370 packet format:
     * [55 AA] [header] [length] [chip_addr] [reg_addr] [data...] [crc5]
     * Length = total bytes after preamble (header + length + chip_addr + reg + data + crc) */
    size_t body_len = 5 + data_len;  /* header + length + chip_addr + reg + data + crc */
    size_t total = 2 + body_len;     /* preamble + body */

    if (!buf || buf_len < total) {
        return -1;
    }

    buf[0] = ASIC_PREAMBLE_1;
    buf[1] = ASIC_PREAMBLE_2;
    buf[2] = ASIC_TYPE_CMD | group | cmd;
    buf[3] = (uint8_t)body_len;
    buf[4] = chip_addr;
    buf[5] = reg_addr;

    if (data && data_len > 0) {
        memcpy(&buf[6], data, data_len);
    }

    /* CRC5 over all body bytes except the CRC itself */
    buf[total - 1] = asic_crc5(&buf[2], body_len - 1);

    return (int)total;
}

int asic_build_job(uint8_t *buf, size_t buf_len,
                   const uint8_t *job_data, size_t job_len)
{
    size_t total = 4 + job_len + 2;

    if (!buf || buf_len < total || !job_data) {
        return -1;
    }

    buf[0] = ASIC_PREAMBLE_1;
    buf[1] = ASIC_PREAMBLE_2;
    /* Header: TYPE_JOB | GROUP_SINGLE | CMD_WRITE = 0x21 (matches forge-os) */
    buf[2] = ASIC_TYPE_JOB | ASIC_GROUP_SINGLE | ASIC_CMD_WRITE;
    /* Length = total bytes after preamble: header(1) + len(1) + data + crc(2) */
    buf[3] = (uint8_t)(job_len + 4);

    memcpy(&buf[4], job_data, job_len);

    /* CRC16 over buf[2..end-2] (header, length, job_data) */
    uint16_t crc = asic_crc16(&buf[2], 2 + job_len);
    buf[total - 2] = (uint8_t)(crc >> 8);   /* big-endian high byte */
    buf[total - 1] = (uint8_t)(crc & 0xFF); /* big-endian low byte */

    return (int)total;
}

int asic_is_nonce_response(const uint8_t *resp)
{
    if (!resp) {
        return 0;
    }

    return (resp[10] & 0x80) ? 1 : 0;
}
