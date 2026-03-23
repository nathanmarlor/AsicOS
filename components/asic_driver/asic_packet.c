#include "asic_packet.h"
#include <string.h>

uint8_t asic_crc5(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x1F;

    for (size_t i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            uint8_t old_bit4 = (crc >> 4) & 1;
            uint8_t data_bit = (data[i] >> bit) & 1;
            crc = ((crc << 1) | data_bit) & 0x1F;
            if (old_bit4 != data_bit) {
                crc ^= 0x05;
            }
        }
    }

    return crc;
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
    buf[2] = ASIC_TYPE_JOB | ASIC_GROUP_ALL;
    buf[3] = (uint8_t)(job_len + 2);

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
