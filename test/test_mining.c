/*
 * Host-side tests for mining functions (double-SHA256, difficulty, merkle root, block header).
 * Uses OpenSSL for SHA256 on macOS.
 *
 * Compile: gcc -Wall -Wextra -I../components/mining/include -o test_mining test_mining.c
 *          -I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto -lm
 * Run:     ./test_mining
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <openssl/sha.h>

/* Re-implement mining functions using OpenSSL instead of mbedTLS,
 * so we can test the logic without ESP-IDF. */

static void sha256_hash(const uint8_t *data, size_t len, uint8_t *out)
{
    SHA256(data, len, out);
}

static void mining_double_sha256(const uint8_t *data, size_t len, uint8_t *out)
{
    uint8_t tmp[32];
    sha256_hash(data, len, tmp);
    sha256_hash(tmp, 32, out);
}

static const double TRUEDIFFONE =
    26959535291011309493156476344723991336010898738574164086137773096960.0;
static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
static const double bits128 = 340282366920938463463374607431768211456.0;
static const double bits64  = 18446744073709551616.0;

static double le256todouble(const void *target)
{
    uint64_t *data64;
    double dcut64;

    data64 = (uint64_t *)((const uint8_t *)target + 24);
    dcut64 = *data64 * bits192;
    data64 = (uint64_t *)((const uint8_t *)target + 16);
    dcut64 += *data64 * bits128;
    data64 = (uint64_t *)((const uint8_t *)target + 8);
    dcut64 += *data64 * bits64;
    data64 = (uint64_t *)((const uint8_t *)target);
    dcut64 += *data64;

    return dcut64;
}

static double mining_difficulty_from_hash(const uint8_t *hash)
{
    return TRUEDIFFONE / le256todouble(hash);
}

static void mining_compute_merkle_root(const uint8_t *coinbase_hash,
                                       const uint8_t (*branches)[32],
                                       int branch_count,
                                       uint8_t *root_out)
{
    uint8_t current[32], concat[64];
    memcpy(current, coinbase_hash, 32);
    for (int i = 0; i < branch_count; i++) {
        memcpy(concat, current, 32);
        memcpy(concat + 32, branches[i], 32);
        mining_double_sha256(concat, 64, current);
    }
    memcpy(root_out, current, 32);
}

static void mining_build_block_header(uint8_t *header_out,
                                      uint32_t version,
                                      const uint8_t *prev_hash,
                                      const uint8_t *merkle_root,
                                      uint32_t ntime,
                                      uint32_t nbits,
                                      uint32_t nonce)
{
    header_out[0] = (uint8_t)(version >>  0);
    header_out[1] = (uint8_t)(version >>  8);
    header_out[2] = (uint8_t)(version >> 16);
    header_out[3] = (uint8_t)(version >> 24);
    memcpy(header_out + 4, prev_hash, 32);
    memcpy(header_out + 36, merkle_root, 32);
    header_out[68] = (uint8_t)(ntime >>  0);
    header_out[69] = (uint8_t)(ntime >>  8);
    header_out[70] = (uint8_t)(ntime >> 16);
    header_out[71] = (uint8_t)(ntime >> 24);
    header_out[72] = (uint8_t)(nbits >>  0);
    header_out[73] = (uint8_t)(nbits >>  8);
    header_out[74] = (uint8_t)(nbits >> 16);
    header_out[75] = (uint8_t)(nbits >> 24);
    header_out[76] = (uint8_t)(nonce >>  0);
    header_out[77] = (uint8_t)(nonce >>  8);
    header_out[78] = (uint8_t)(nonce >> 16);
    header_out[79] = (uint8_t)(nonce >> 24);
}

/* ── Helpers ─────────────────────────────────────────────────────── */

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

static void reverse_bytes(uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t t = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = t;
    }
}

static void print_hex(const char *label, const uint8_t *data, size_t len)
{
    printf("  %s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

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
 * Test: double-SHA256 with known input
 * ================================================================ */
static void test_double_sha256(void)
{
    printf("\n=== double_sha256: known input ===\n");

    /* SHA256(SHA256("")) should be a known value.
     * SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
     * SHA256(above) = 5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456 */
    uint8_t hash[32];
    mining_double_sha256((const uint8_t *)"", 0, hash);

    const char *expected_hex = "5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456";
    uint8_t expected[32];
    hex_to_bytes(expected_hex, expected, 32);

    print_hex("hash    ", hash, 32);
    print_hex("expected", expected, 32);
    TEST_ASSERT(memcmp(hash, expected, 32) == 0, "double-SHA256 of empty string");
}

static void test_double_sha256_hello(void)
{
    printf("\n=== double_sha256: 'hello' ===\n");

    uint8_t hash[32];
    mining_double_sha256((const uint8_t *)"hello", 5, hash);

    /* Verify it's not all zeros and is deterministic */
    uint8_t zeros[32] = {0};
    TEST_ASSERT(memcmp(hash, zeros, 32) != 0, "hash of 'hello' is not all zeros");

    uint8_t hash2[32];
    mining_double_sha256((const uint8_t *)"hello", 5, hash2);
    TEST_ASSERT(memcmp(hash, hash2, 32) == 0, "double-SHA256 is deterministic");
}

/* ================================================================
 * Test: difficulty from known Bitcoin block hashes
 * ================================================================ */
static void test_difficulty_genesis(void)
{
    printf("\n=== difficulty: genesis block ===\n");

    uint8_t prev_hash[32] = {0};
    const char *merkle_hex = "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b";
    uint8_t merkle_root[32];
    hex_to_bytes(merkle_hex, merkle_root, 32);
    reverse_bytes(merkle_root, 32);

    uint8_t header[80];
    mining_build_block_header(header, 1, prev_hash, merkle_root,
                              0x495fab29, 0x1d00ffff, 2083236893);

    uint8_t hash[32];
    mining_double_sha256(header, 80, hash);

    double diff = mining_difficulty_from_hash(hash);
    printf("  Genesis block difficulty: %.4f\n", diff);
    TEST_ASSERT(diff >= 1.0, "genesis block difficulty >= 1");
    /* The genesis block hash exceeds the diff-1 target, so its share
     * difficulty (truediffone / hash_value) is well above 1. */
    TEST_ASSERT(diff > 1.0 && diff < 1e6, "genesis block share difficulty in reasonable range");
}

static void test_difficulty_block_125552(void)
{
    printf("\n=== difficulty: block 125552 ===\n");

    const char *prev_display = "00000000000008a3a41b85b8b29ad444def299fee21793cd8b9e567eab02cd81";
    uint8_t prev_hash[32];
    hex_to_bytes(prev_display, prev_hash, 32);
    reverse_bytes(prev_hash, 32);

    const char *merkle_display = "2b12fcf1b09288fcaff797d71e950e71ae42b91e8bdb2304758dfcffc2b620e3";
    uint8_t merkle_root[32];
    hex_to_bytes(merkle_display, merkle_root, 32);
    reverse_bytes(merkle_root, 32);

    uint8_t header[80];
    mining_build_block_header(header, 1, prev_hash, merkle_root,
                              0x4dd7f5c7, 0x1a44b9f2, 2504433986);

    uint8_t hash[32];
    mining_double_sha256(header, 80, hash);

    double diff = mining_difficulty_from_hash(hash);
    printf("  Block 125552 difficulty: %.4f\n", diff);
    TEST_ASSERT(diff > 244000, "block 125552 difficulty > 244000");
}

/* ================================================================
 * Test: merkle root computation
 * ================================================================ */
static void test_merkle_root_no_branches(void)
{
    printf("\n=== merkle root: no branches ===\n");

    uint8_t coinbase_hash[32];
    memset(coinbase_hash, 0xAB, 32);

    uint8_t root[32];
    mining_compute_merkle_root(coinbase_hash, NULL, 0, root);

    TEST_ASSERT(memcmp(root, coinbase_hash, 32) == 0,
                "merkle root with no branches equals coinbase hash");
}

static void test_merkle_root_one_branch(void)
{
    printf("\n=== merkle root: one branch ===\n");

    uint8_t coinbase_hash[32] = {0};
    coinbase_hash[0] = 0x01;

    uint8_t branch[1][32] = {{0}};
    branch[0][0] = 0x02;

    uint8_t root[32];
    mining_compute_merkle_root(coinbase_hash, branch, 1, root);

    /* Manually compute: double_sha256(coinbase_hash || branch[0]) */
    uint8_t concat[64];
    memcpy(concat, coinbase_hash, 32);
    memcpy(concat + 32, branch[0], 32);
    uint8_t expected[32];
    mining_double_sha256(concat, 64, expected);

    TEST_ASSERT(memcmp(root, expected, 32) == 0,
                "merkle root with one branch matches manual computation");
}

/* ================================================================
 * Test: block header is exactly 80 bytes with correct layout
 * ================================================================ */
static void test_block_header_layout(void)
{
    printf("\n=== block header: layout check ===\n");

    uint8_t prev_hash[32], merkle_root[32];
    memset(prev_hash, 0xAA, 32);
    memset(merkle_root, 0xBB, 32);

    uint8_t header[80];
    mining_build_block_header(header, 0x20000000, prev_hash, merkle_root,
                              0x12345678, 0xABCDEF01, 0xDEADBEEF);

    /* Version: little-endian 0x20000000 */
    TEST_ASSERT(header[0] == 0x00, "version byte 0");
    TEST_ASSERT(header[1] == 0x00, "version byte 1");
    TEST_ASSERT(header[2] == 0x00, "version byte 2");
    TEST_ASSERT(header[3] == 0x20, "version byte 3");

    /* prev_hash at offset 4 */
    TEST_ASSERT(header[4] == 0xAA, "prev_hash starts at offset 4");
    TEST_ASSERT(header[35] == 0xAA, "prev_hash ends at offset 35");

    /* merkle_root at offset 36 */
    TEST_ASSERT(header[36] == 0xBB, "merkle_root starts at offset 36");
    TEST_ASSERT(header[67] == 0xBB, "merkle_root ends at offset 67");

    /* ntime at offset 68: LE 0x12345678 */
    TEST_ASSERT(header[68] == 0x78, "ntime byte 0");
    TEST_ASSERT(header[69] == 0x56, "ntime byte 1");
    TEST_ASSERT(header[70] == 0x34, "ntime byte 2");
    TEST_ASSERT(header[71] == 0x12, "ntime byte 3");

    /* nbits at offset 72: LE 0xABCDEF01 */
    TEST_ASSERT(header[72] == 0x01, "nbits byte 0");
    TEST_ASSERT(header[73] == 0xEF, "nbits byte 1");

    /* nonce at offset 76: LE 0xDEADBEEF */
    TEST_ASSERT(header[76] == 0xEF, "nonce byte 0");
    TEST_ASSERT(header[77] == 0xBE, "nonce byte 1");
    TEST_ASSERT(header[78] == 0xAD, "nonce byte 2");
    TEST_ASSERT(header[79] == 0xDE, "nonce byte 3");
}

/* ================================================================ */
int main(void)
{
    printf("Mining Function Tests\n");
    printf("=====================\n");

    test_double_sha256();
    test_double_sha256_hello();
    test_difficulty_genesis();
    test_difficulty_block_125552();
    test_merkle_root_no_branches();
    test_merkle_root_one_branch();
    test_block_header_layout();

    printf("\n=====================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
