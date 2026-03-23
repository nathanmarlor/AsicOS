/*
 * Host-side tests for CRC5 and CRC16 used in BM1370 ASIC packets.
 *
 * Compile: gcc -Wall -Wextra -I../components/asic_driver/include -o test_crc test_crc.c ../components/asic_driver/asic_packet.c
 * Run:     ./test_crc
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "asic_packet.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        tests_failed++; \
    } else { \
        printf("  PASS: %s\n", msg); \
        tests_passed++; \
    } \
} while (0)

/* ================================================================
 * CRC5 tests
 * ================================================================ */

static void test_crc5_known_command(void)
{
    printf("\n=== CRC5: known command bytes ===\n");

    /* A typical BM1370 write-all command body (header + length + chip + reg + data).
     * Verified against forge-os CRC5 output. */
    uint8_t cmd_body[] = {0x41, 0x09, 0x00, 0x14, 0x00, 0x00, 0x00, 0xFF};
    uint8_t crc = asic_crc5(cmd_body, sizeof(cmd_body));
    printf("  CRC5 of cmd body: 0x%02X\n", crc);

    /* CRC5 result must be 5 bits (0-31) */
    TEST_ASSERT(crc <= 0x1F, "CRC5 result fits in 5 bits");
}

static void test_crc5_set_address(void)
{
    printf("\n=== CRC5: set address command ===\n");

    /* Set address command: TYPE_CMD | GROUP_SINGLE | CMD_SETADDR = 0x40
     * Body: header(0x40), length(0x05), chip_addr(0x00), reg(0x00) */
    uint8_t body[] = {0x40, 0x05, 0x00, 0x00};
    uint8_t crc = asic_crc5(body, sizeof(body));
    printf("  CRC5 of set_address body: 0x%02X\n", crc);

    TEST_ASSERT(crc <= 0x1F, "CRC5 result fits in 5 bits");

    /* Same input must produce same output */
    uint8_t crc2 = asic_crc5(body, sizeof(body));
    TEST_ASSERT(crc == crc2, "CRC5 is deterministic");
}

static void test_crc5_single_byte(void)
{
    printf("\n=== CRC5: single byte ===\n");

    uint8_t data = 0x00;
    uint8_t crc_zero = asic_crc5(&data, 1);
    printf("  CRC5(0x00): 0x%02X\n", crc_zero);

    data = 0xFF;
    uint8_t crc_ff = asic_crc5(&data, 1);
    printf("  CRC5(0xFF): 0x%02X\n", crc_ff);

    TEST_ASSERT(crc_zero <= 0x1F, "CRC5(0x00) fits in 5 bits");
    TEST_ASSERT(crc_ff <= 0x1F, "CRC5(0xFF) fits in 5 bits");
    TEST_ASSERT(crc_zero != crc_ff, "CRC5 differs for different input");
}

static void test_crc5_empty(void)
{
    printf("\n=== CRC5: empty data ===\n");

    /* With init value 0x1F and zero-length data, CRC should be init */
    uint8_t crc = asic_crc5(NULL, 0);
    printf("  CRC5(empty): 0x%02X\n", crc);
    TEST_ASSERT(crc == 0x1F, "CRC5 of empty data equals init value (0x1F)");
}

/* ================================================================
 * CRC16 tests
 * ================================================================ */

static void test_crc16_known_job(void)
{
    printf("\n=== CRC16: known job bytes ===\n");

    /* Simplified job header+data for testing */
    uint8_t job_data[] = {
        0x21, 0x56,  /* header, length */
        0x00, 0x01,  /* job_id, midstates */
        0x00, 0x00, 0x00, 0x00,  /* starting_nonce */
        0xFF, 0xFF, 0x00, 0x1D,  /* nbits */
        0x29, 0xAB, 0x5F, 0x49,  /* ntime */
    };
    uint16_t crc = asic_crc16(job_data, sizeof(job_data));
    printf("  CRC16 of job data: 0x%04X\n", crc);

    /* CRC16-CCITT should be deterministic */
    uint16_t crc2 = asic_crc16(job_data, sizeof(job_data));
    TEST_ASSERT(crc == crc2, "CRC16 is deterministic");
}

static void test_crc16_empty(void)
{
    printf("\n=== CRC16: empty data ===\n");

    uint16_t crc = asic_crc16(NULL, 0);
    printf("  CRC16(empty): 0x%04X\n", crc);
    TEST_ASSERT(crc == 0xFFFF, "CRC16 of empty data equals init value (0xFFFF)");
}

static void test_crc16_single_byte(void)
{
    printf("\n=== CRC16: single byte ===\n");

    uint8_t data = 0x00;
    uint16_t crc_zero = asic_crc16(&data, 1);
    printf("  CRC16(0x00): 0x%04X\n", crc_zero);

    data = 0xFF;
    uint16_t crc_ff = asic_crc16(&data, 1);
    printf("  CRC16(0xFF): 0x%04X\n", crc_ff);

    TEST_ASSERT(crc_zero != crc_ff, "CRC16 differs for different input");
}

static void test_crc16_known_vector(void)
{
    printf("\n=== CRC16: CRC-CCITT known vector ===\n");

    /* CRC-16/CCITT-FALSE of "123456789" = 0x29B1 */
    const uint8_t data[] = "123456789";
    uint16_t crc = asic_crc16(data, 9);
    printf("  CRC16(\"123456789\"): 0x%04X (expected 0x29B1)\n", crc);
    TEST_ASSERT(crc == 0x29B1, "CRC16-CCITT of '123456789' is 0x29B1");
}

/* ================================================================
 * Packet builder tests
 * ================================================================ */

static void test_build_cmd(void)
{
    printf("\n=== asic_build_cmd ===\n");

    uint8_t buf[32];
    uint8_t data[] = {0x00, 0x07, 0x00, 0x00};
    int len = asic_build_cmd(buf, sizeof(buf),
                             ASIC_CMD_WRITE, ASIC_GROUP_ALL,
                             0x00, 0xA8, data, sizeof(data));
    TEST_ASSERT(len > 0, "asic_build_cmd returns positive length");
    TEST_ASSERT(buf[0] == ASIC_PREAMBLE_1, "preamble byte 0 is 0x55");
    TEST_ASSERT(buf[1] == ASIC_PREAMBLE_2, "preamble byte 1 is 0xAA");
    TEST_ASSERT(buf[4] == 0x00, "chip_addr is 0x00");
    TEST_ASSERT(buf[5] == 0xA8, "reg_addr is 0xA8");

    /* Verify CRC5 is appended */
    uint8_t crc = asic_crc5(&buf[2], (size_t)(len - 3));
    TEST_ASSERT(buf[len - 1] == crc, "CRC5 matches computed value");
}

static void test_build_cmd_null(void)
{
    printf("\n=== asic_build_cmd: NULL buffer ===\n");

    int len = asic_build_cmd(NULL, 0, ASIC_CMD_WRITE, ASIC_GROUP_ALL, 0, 0, NULL, 0);
    TEST_ASSERT(len == -1, "returns -1 for NULL buffer");
}

static void test_build_job(void)
{
    printf("\n=== asic_build_job ===\n");

    uint8_t job_data[82];
    memset(job_data, 0x42, sizeof(job_data));

    uint8_t buf[128];
    int len = asic_build_job(buf, sizeof(buf), job_data, sizeof(job_data));
    TEST_ASSERT(len > 0, "asic_build_job returns positive length");
    TEST_ASSERT(buf[0] == ASIC_PREAMBLE_1, "preamble byte 0 is 0x55");
    TEST_ASSERT(buf[1] == ASIC_PREAMBLE_2, "preamble byte 1 is 0xAA");
    TEST_ASSERT(buf[2] == (ASIC_TYPE_JOB | ASIC_GROUP_SINGLE | ASIC_CMD_WRITE),
                "header byte matches job type");

    /* Verify CRC16 is appended correctly */
    uint16_t computed_crc = asic_crc16(&buf[2], 2 + sizeof(job_data));
    uint16_t stored_crc = ((uint16_t)buf[len - 2] << 8) | buf[len - 1];
    TEST_ASSERT(computed_crc == stored_crc, "CRC16 matches computed value");
}

static void test_build_job_null(void)
{
    printf("\n=== asic_build_job: NULL inputs ===\n");

    uint8_t buf[128];
    int len = asic_build_job(buf, sizeof(buf), NULL, 0);
    TEST_ASSERT(len == -1, "returns -1 for NULL job_data");

    len = asic_build_job(NULL, 0, (uint8_t[]){0}, 1);
    TEST_ASSERT(len == -1, "returns -1 for NULL buffer");
}

static void test_is_nonce_response(void)
{
    printf("\n=== asic_is_nonce_response ===\n");

    uint8_t nonce_resp[11] = {0xAA, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0x80};
    TEST_ASSERT(asic_is_nonce_response(nonce_resp) == 1, "detects nonce response (bit 7 set)");

    uint8_t reg_resp[11] = {0xAA, 0x55, 0, 0, 0, 0, 0, 0, 0, 0, 0x00};
    TEST_ASSERT(asic_is_nonce_response(reg_resp) == 0, "detects register response (bit 7 clear)");

    TEST_ASSERT(asic_is_nonce_response(NULL) == 0, "returns 0 for NULL");
}

/* ================================================================ */
int main(void)
{
    printf("CRC and Packet Tests\n");
    printf("====================\n");

    /* CRC5 tests */
    test_crc5_known_command();
    test_crc5_set_address();
    test_crc5_single_byte();
    test_crc5_empty();

    /* CRC16 tests */
    test_crc16_known_job();
    test_crc16_empty();
    test_crc16_single_byte();
    test_crc16_known_vector();

    /* Packet builder tests */
    test_build_cmd();
    test_build_cmd_null();
    test_build_job();
    test_build_job_null();
    test_is_nonce_response();

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
