/*
 * Host-side tests for stratum message building and hex utilities.
 *
 * Tests stratum_build_* functions (which are pure snprintf, no cJSON needed)
 * and hex_to_bytes / bytes_to_hex from stratum_job.c.
 *
 * Compile: gcc -Wall -Wextra -I../components/stratum/include
 *          -I../components/asic_driver/include -I../components/mining/include
 *          -o test_stratum test_stratum.c
 *          -I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib
 *          -lssl -lcrypto -lm
 * Run:     ./test_stratum
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>

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

/* ── Inline the hex utility functions from stratum_job.c ───────── */

static int hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;
    size_t byte_len = hex_len / 2;
    if (byte_len > out_len) return -1;
    for (size_t i = 0; i < byte_len; i++) {
        unsigned int val;
        char tmp[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        if (sscanf(tmp, "%02x", &val) != 1) return -1;
        out[i] = (uint8_t)val;
    }
    return (int)byte_len;
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(out + i * 2, "%02x", data[i]);
    }
    out[len * 2] = '\0';
}

/* ── Inline stratum_build_subscribe (pure snprintf, no cJSON) ──── */

static int s_msg_id = 1;

static int stratum_build_subscribe(char *buf, size_t buf_len)
{
    int id = s_msg_id++;
    int n = snprintf(buf, buf_len,
        "{\"id\":%d,\"method\":\"mining.subscribe\",\"params\":[]}\n", id);
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

static int stratum_build_authorize(char *buf, size_t buf_len,
                                   const char *user, const char *pass)
{
    int id = s_msg_id++;
    int n = snprintf(buf, buf_len,
        "{\"id\":%d,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",
        id, user, pass);
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

static int stratum_build_submit(char *buf, size_t buf_len,
                                const char *user, const char *job_id,
                                const char *extranonce2, const char *ntime,
                                const char *nonce, const char *version_bits)
{
    int id = s_msg_id++;
    int n;
    if (version_bits && version_bits[0]) {
        n = snprintf(buf, buf_len,
            "{\"id\":%d,\"method\":\"mining.submit\","
            "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            id, user, job_id, extranonce2, ntime, nonce, version_bits);
    } else {
        n = snprintf(buf, buf_len,
            "{\"id\":%d,\"method\":\"mining.submit\","
            "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            id, user, job_id, extranonce2, ntime, nonce);
    }
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

static int stratum_build_suggest_difficulty(char *buf, size_t buf_len, double diff)
{
    int id = s_msg_id++;
    int n = snprintf(buf, buf_len,
        "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%g]}\n",
        id, diff);
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

/* ================================================================
 * hex_to_bytes / bytes_to_hex tests
 * ================================================================ */

static void test_hex_roundtrip(void)
{
    printf("\n=== hex_to_bytes / bytes_to_hex: round-trip ===\n");

    const char *original = "deadbeef01020304ff00";
    uint8_t bytes[10];
    int n = hex_to_bytes(original, bytes, sizeof(bytes));
    TEST_ASSERT(n == 10, "hex_to_bytes returns correct length");

    char hex_out[21];
    bytes_to_hex(bytes, 10, hex_out);
    TEST_ASSERT(strcmp(hex_out, original) == 0, "round-trip produces original hex");
}

static void test_hex_to_bytes_empty(void)
{
    printf("\n=== hex_to_bytes: empty string ===\n");

    uint8_t bytes[1];
    int n = hex_to_bytes("", bytes, sizeof(bytes));
    TEST_ASSERT(n == 0, "empty hex string returns 0 bytes");
}

static void test_hex_to_bytes_odd_length(void)
{
    printf("\n=== hex_to_bytes: odd length ===\n");

    uint8_t bytes[4];
    int n = hex_to_bytes("abc", bytes, sizeof(bytes));
    TEST_ASSERT(n == -1, "odd-length hex string returns -1");
}

static void test_hex_to_bytes_overflow(void)
{
    printf("\n=== hex_to_bytes: output buffer too small ===\n");

    uint8_t bytes[2];
    int n = hex_to_bytes("aabbccdd", bytes, sizeof(bytes));
    TEST_ASSERT(n == -1, "returns -1 when output buffer too small");
}

static void test_hex_to_bytes_known_values(void)
{
    printf("\n=== hex_to_bytes: known values ===\n");

    uint8_t bytes[4];
    int n = hex_to_bytes("00ff80a5", bytes, sizeof(bytes));
    TEST_ASSERT(n == 4, "parses 4 bytes");
    TEST_ASSERT(bytes[0] == 0x00, "byte 0 is 0x00");
    TEST_ASSERT(bytes[1] == 0xFF, "byte 1 is 0xFF");
    TEST_ASSERT(bytes[2] == 0x80, "byte 2 is 0x80");
    TEST_ASSERT(bytes[3] == 0xA5, "byte 3 is 0xA5");
}

static void test_bytes_to_hex_single(void)
{
    printf("\n=== bytes_to_hex: single byte ===\n");

    uint8_t byte = 0xAB;
    char hex[3];
    bytes_to_hex(&byte, 1, hex);
    TEST_ASSERT(strcmp(hex, "ab") == 0, "single byte 0xAB -> 'ab'");
}

/* ================================================================
 * stratum_build_subscribe test
 * ================================================================ */

static void test_build_subscribe(void)
{
    printf("\n=== stratum_build_subscribe ===\n");

    char buf[256];
    s_msg_id = 1;  /* reset for predictable output */
    int len = stratum_build_subscribe(buf, sizeof(buf));

    TEST_ASSERT(len > 0, "returns positive length");
    TEST_ASSERT(strstr(buf, "\"method\":\"mining.subscribe\"") != NULL,
                "contains mining.subscribe method");
    TEST_ASSERT(strstr(buf, "\"params\":[]") != NULL,
                "contains empty params array");
    TEST_ASSERT(strstr(buf, "\"id\":1") != NULL,
                "contains id:1");
    TEST_ASSERT(buf[len - 1] == '\n', "ends with newline");

    printf("  Output: %s", buf);
}

static void test_build_subscribe_small_buffer(void)
{
    printf("\n=== stratum_build_subscribe: small buffer ===\n");

    char buf[10];
    s_msg_id = 1;
    int len = stratum_build_subscribe(buf, sizeof(buf));
    TEST_ASSERT(len == -1, "returns -1 for too-small buffer");
}

/* ================================================================
 * stratum_build_authorize test
 * ================================================================ */

static void test_build_authorize(void)
{
    printf("\n=== stratum_build_authorize ===\n");

    char buf[256];
    s_msg_id = 1;
    int len = stratum_build_authorize(buf, sizeof(buf), "worker.1", "x");

    TEST_ASSERT(len > 0, "returns positive length");
    TEST_ASSERT(strstr(buf, "\"method\":\"mining.authorize\"") != NULL,
                "contains mining.authorize method");
    TEST_ASSERT(strstr(buf, "\"worker.1\"") != NULL, "contains username");
    TEST_ASSERT(strstr(buf, "\"x\"") != NULL, "contains password");
    printf("  Output: %s", buf);
}

/* ================================================================
 * stratum_build_submit test
 * ================================================================ */

static void test_build_submit(void)
{
    printf("\n=== stratum_build_submit ===\n");

    char buf[512];
    s_msg_id = 1;
    int len = stratum_build_submit(buf, sizeof(buf),
        "worker.1", "abc123", "00000001",
        "5f5e1001", "deadbeef", "00002000");

    TEST_ASSERT(len > 0, "returns positive length");
    TEST_ASSERT(strstr(buf, "\"method\":\"mining.submit\"") != NULL,
                "contains mining.submit method");
    TEST_ASSERT(strstr(buf, "\"abc123\"") != NULL, "contains job_id");
    TEST_ASSERT(strstr(buf, "\"00000001\"") != NULL, "contains extranonce2");
    TEST_ASSERT(strstr(buf, "\"deadbeef\"") != NULL, "contains nonce");
    TEST_ASSERT(strstr(buf, "\"00002000\"") != NULL, "contains version_bits");
    printf("  Output: %s", buf);
}

static void test_build_submit_no_version_bits(void)
{
    printf("\n=== stratum_build_submit: no version bits ===\n");

    char buf[512];
    s_msg_id = 1;
    int len = stratum_build_submit(buf, sizeof(buf),
        "worker.1", "abc123", "00000001",
        "5f5e1001", "deadbeef", "");

    TEST_ASSERT(len > 0, "returns positive length");
    /* Should have 5 params, not 6 */
    int comma_count = 0;
    const char *p = strstr(buf, "\"params\":[");
    if (p) {
        p += 10;
        while (*p && *p != ']') {
            if (*p == ',') comma_count++;
            p++;
        }
    }
    TEST_ASSERT(comma_count == 4, "5 params (4 commas) when version_bits empty");
    printf("  Output: %s", buf);
}

/* ================================================================
 * stratum_build_suggest_difficulty test
 * ================================================================ */

static void test_build_suggest_difficulty(void)
{
    printf("\n=== stratum_build_suggest_difficulty ===\n");

    char buf[256];
    s_msg_id = 1;
    int len = stratum_build_suggest_difficulty(buf, sizeof(buf), 256.0);

    TEST_ASSERT(len > 0, "returns positive length");
    TEST_ASSERT(strstr(buf, "\"method\":\"mining.suggest_difficulty\"") != NULL,
                "contains correct method");
    TEST_ASSERT(strstr(buf, "256") != NULL, "contains difficulty value");
    printf("  Output: %s", buf);
}

/* ================================================================ */
int main(void)
{
    printf("Stratum Message Tests\n");
    printf("=====================\n");

    /* Hex utilities */
    test_hex_roundtrip();
    test_hex_to_bytes_empty();
    test_hex_to_bytes_odd_length();
    test_hex_to_bytes_overflow();
    test_hex_to_bytes_known_values();
    test_bytes_to_hex_single();

    /* Build functions */
    test_build_subscribe();
    test_build_subscribe_small_buffer();
    test_build_authorize();
    test_build_submit();
    test_build_submit_no_version_bits();
    test_build_suggest_difficulty();

    printf("\n=====================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
