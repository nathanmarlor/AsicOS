#pragma once

#include <stdint.h>
#include <stddef.h>

// Preamble
#define ASIC_PREAMBLE_1     0x55
#define ASIC_PREAMBLE_2     0xAA

// Header type bits
#define ASIC_TYPE_JOB       0x20
#define ASIC_TYPE_CMD       0x40

// Group bits
#define ASIC_GROUP_SINGLE   0x00
#define ASIC_GROUP_ALL      0x10

// Command types
#define ASIC_CMD_SETADDR    0x00
#define ASIC_CMD_WRITE      0x01
#define ASIC_CMD_READ       0x02
#define ASIC_CMD_INACTIVE   0x03

// Response size
#define ASIC_RESP_SIZE      11

// CRC functions
uint8_t  asic_crc5(const uint8_t *data, size_t len);
uint16_t asic_crc16(const uint8_t *data, size_t len);

// Build a command packet. Returns total packet length, or -1 on error.
int asic_build_cmd(uint8_t *buf, size_t buf_len,
                   uint8_t cmd, uint8_t group,
                   uint8_t reg_addr,
                   const uint8_t *data, size_t data_len);

// Build a job packet. Returns total packet length, or -1 on error.
int asic_build_job(uint8_t *buf, size_t buf_len,
                   const uint8_t *job_data, size_t job_len);

// Check if response is a nonce result (vs register read). Returns 1 if nonce.
int asic_is_nonce_response(const uint8_t *resp);
