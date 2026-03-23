/*
 * Host-side test for nonce verification logic.
 *
 * Verifies the double-SHA256 + difficulty calculation using known
 * Bitcoin blocks as test vectors, and tests the swap_endian_words
 * transformation needed to convert stratum prev_block_hash to block
 * header format.
 *
 * Compile:
 *   gcc -o /tmp/test_nonce test/test_nonce_verify.c \
 *       -I/opt/homebrew/opt/openssl@3/include \
 *       -L/opt/homebrew/opt/openssl@3/lib \
 *       -lssl -lcrypto -lm
 * Run:
 *   /tmp/test_nonce
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <openssl/sha.h>

/* ── SHA256 helpers ─────────────────────────────────────────────── */
static void sha256_hash(const uint8_t *data, size_t len, uint8_t *out)
{
    SHA256(data, len, out);
}

static void double_sha256(const uint8_t *data, size_t len, uint8_t *out)
{
    uint8_t tmp[32];
    sha256_hash(data, len, tmp);
    sha256_hash(tmp, 32, out);
}

/* ── le256todouble  (matches forge-os exactly) ──────────────────── */
static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
static const double bits128 = 340282366920938463463374607431768211456.0;
static const double bits64  = 18446744073709551616.0;
static const double truediffone =
    26959535291011309493156476344723991336010898738574164086137773096960.0;

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

static double difficulty_from_hash(const uint8_t *hash)
{
    double s64 = le256todouble(hash);
    return truediffone / s64;
}

/* ── Byte-order helpers ─────────────────────────────────────────── */
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

static void print_hex(const char *label, const uint8_t *data, size_t len)
{
    printf("  %s: ", label);
    for (size_t i = 0; i < len; i++) printf("%02x", data[i]);
    printf("\n");
}

static void reverse_bytes(uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len / 2; i++) {
        uint8_t t = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = t;
    }
}

/*
 * swap_endian_words_bin: reverse bytes within each 4-byte word of a
 * binary buffer.  This is the transformation needed to convert stratum's
 * prev_block_hash from wire format to internal byte order for hashing.
 *
 * Matches forge-os swap_endian_words() but operates on binary instead
 * of hex strings.
 */
static void swap_endian_words_bin(const uint8_t *in, uint8_t *out, size_t len)
{
    for (size_t i = 0; i < len; i += 4) {
        out[i + 0] = in[i + 3];
        out[i + 1] = in[i + 2];
        out[i + 2] = in[i + 1];
        out[i + 3] = in[i + 0];
    }
}

/* ── Build block header (matches forge-os test_nonce_value) ─────── */
static void build_block_header(uint8_t *header,
                                uint32_t version,
                                const uint8_t *prev_hash,
                                const uint8_t *merkle_root,
                                uint32_t ntime,
                                uint32_t nbits,
                                uint32_t nonce)
{
    memcpy(header,      &version, 4);
    memcpy(header + 4,  prev_hash, 32);
    memcpy(header + 36, merkle_root, 32);
    memcpy(header + 68, &ntime, 4);
    memcpy(header + 72, &nbits, 4);
    memcpy(header + 76, &nonce, 4);
}

/* ── Test counters ──────────────────────────────────────────────── */
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
 * Test 1: Bitcoin genesis block (block 0) - simplest test vector
 * ================================================================ */
static void test_genesis_block(void)
{
    printf("\n=== Test 1: Bitcoin genesis block (block 0) ===\n");

    uint8_t prev_hash[32] = {0};  /* all zeros */

    const char *merkle_display =
        "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b";
    uint8_t merkle_root[32];
    hex_to_bytes(merkle_display, merkle_root, 32);
    reverse_bytes(merkle_root, 32);  /* display -> internal LE */

    uint8_t header[80];
    build_block_header(header, 1, prev_hash, merkle_root,
                       0x495fab29, 0x1d00ffff, 2083236893);

    uint8_t hash[32];
    double_sha256(header, 80, hash);

    /* Expected hash (LE) */
    const char *expected_le =
        "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000";
    uint8_t expected[32];
    hex_to_bytes(expected_le, expected, 32);

    print_hex("hash    ", hash, 32);
    print_hex("expected", expected, 32);

    TEST_ASSERT(memcmp(hash, expected, 32) == 0,
                "genesis block double-SHA256 matches");

    double diff = difficulty_from_hash(hash);
    printf("  Difficulty: %.4f\n", diff);
    TEST_ASSERT(diff >= 1.0, "genesis block difficulty >= 1");
}

/* ================================================================
 * Test 2: Bitcoin block 125552 - classic test vector
 * ================================================================
 *
 * Block 125552:
 *   Version:    1
 *   PrevHash:   00000000000008a3a41b85b8b29ad444def299fee21793cd8b9e567eab02cd81
 *   MerkleRoot: 2b12fcf1b09288fcaff797d71e950e71ae42b91e8bdb2304758dfcffc2b620e3
 *   Timestamp:  1305998791 (0x4dd7f5c7)
 *   Bits:       0x1a44b9f2
 *   Nonce:      2504433986 (0x9546a142)
 *   Hash:       00000000000000001e8d6829a8a21adc5d38d0a473b144b6765798e61f98bd1d
 */
static void test_block_125552(void)
{
    printf("\n=== Test 2: Bitcoin block 125552 ===\n");

    /* Internal byte order (reversed from display) */
    const char *prev_display =
        "00000000000008a3a41b85b8b29ad444def299fee21793cd8b9e567eab02cd81";
    uint8_t prev_hash[32];
    hex_to_bytes(prev_display, prev_hash, 32);
    reverse_bytes(prev_hash, 32);

    const char *merkle_display =
        "2b12fcf1b09288fcaff797d71e950e71ae42b91e8bdb2304758dfcffc2b620e3";
    uint8_t merkle_root[32];
    hex_to_bytes(merkle_display, merkle_root, 32);
    reverse_bytes(merkle_root, 32);

    uint8_t header[80];
    build_block_header(header, 1, prev_hash, merkle_root,
                       0x4dd7f5c7, 0x1a44b9f2, 2504433986);

    uint8_t hash[32];
    double_sha256(header, 80, hash);

    const char *expected_hash_display =
        "00000000000000001e8d6829a8a21adc5d38d0a473b144b6765798e61f98bd1d";
    uint8_t expected[32];
    hex_to_bytes(expected_hash_display, expected, 32);
    reverse_bytes(expected, 32);  /* display -> LE */

    print_hex("hash    ", hash, 32);
    print_hex("expected", expected, 32);

    TEST_ASSERT(memcmp(hash, expected, 32) == 0,
                "block 125552 double-SHA256 matches");

    double diff = difficulty_from_hash(hash);
    printf("  Difficulty: %.4f\n", diff);
    /* Block 125552 network difficulty was ~244,112 but the actual share
     * difficulty (truediffone / hash_value) is much higher since the hash
     * far exceeded the required target.  Just verify it's well above
     * the network difficulty. */
    TEST_ASSERT(diff > 244000,
                "block 125552 difficulty exceeds network difficulty (~244,112)");
}

/* ================================================================
 * Test 3: swap_endian_words converts stratum prev_hash correctly
 * ================================================================
 *
 * Stratum sends prev_block_hash with each 4-byte word byte-swapped
 * from internal byte order.
 *
 * Block 125552 prev_hash:
 *   Internal LE: 81cd02ab 7e569e8b cd9317e2 fe99f2de 44d49ab2 b8851ba4 a3080000 00000000
 *   Stratum:     ab02cd81 8b9e567e e21793cd def299fe b29ad444 a41b85b8 000008a3 00000000
 *
 * After swap_endian_words_bin: should get internal LE back.
 */
static void test_swap_endian_words(void)
{
    printf("\n=== Test 3: swap_endian_words_bin transformation ===\n");

    /* Stratum wire format for block 125552's prev_hash */
    const char *stratum_hex =
        "ab02cd818b9e567ee21793cddef299feb29ad444a41b85b8000008a300000000";
    uint8_t stratum_bytes[32];
    hex_to_bytes(stratum_hex, stratum_bytes, 32);

    uint8_t swapped[32];
    swap_endian_words_bin(stratum_bytes, swapped, 32);

    /* Expected: internal LE byte order */
    const char *expected_hex =
        "81cd02ab7e569e8bcd9317e2fe99f2de44d49ab2b8851ba4a308000000000000";
    uint8_t expected[32];
    hex_to_bytes(expected_hex, expected, 32);

    print_hex("stratum  ", stratum_bytes, 32);
    print_hex("swapped  ", swapped, 32);
    print_hex("expected ", expected, 32);

    TEST_ASSERT(memcmp(swapped, expected, 32) == 0,
                "swap_endian_words_bin converts stratum -> internal LE");

    /* Verify: using swapped prev_hash in a block header produces correct hash */
    const char *merkle_display =
        "2b12fcf1b09288fcaff797d71e950e71ae42b91e8bdb2304758dfcffc2b620e3";
    uint8_t merkle_root[32];
    hex_to_bytes(merkle_display, merkle_root, 32);
    reverse_bytes(merkle_root, 32);

    uint8_t header[80];
    build_block_header(header, 1, swapped, merkle_root,
                       0x4dd7f5c7, 0x1a44b9f2, 2504433986);

    uint8_t hash[32];
    double_sha256(header, 80, hash);

    double diff = difficulty_from_hash(hash);
    printf("  Difficulty using swapped prev_hash: %.4f\n", diff);
    TEST_ASSERT(diff > 200000,
                "correct difficulty after swap_endian_words_bin");
}

/* ================================================================
 * Test 4: BUG REPRODUCTION - without swap, difficulty is near-zero
 * ================================================================
 *
 * This reproduces the actual bug in our code: if hex_to_bytes is
 * used on stratum's prev_block_hash without swap_endian_words_bin,
 * the block header has wrong byte order and produces garbage hash/diff.
 */
static void test_bug_without_swap(void)
{
    printf("\n=== Test 4: Bug reproduction - no swap produces near-zero diff ===\n");

    /* Plain hex_to_bytes of stratum wire format (THE BUG) */
    const char *stratum_hex =
        "ab02cd818b9e567ee21793cddef299feb29ad444a41b85b8000008a300000000";
    uint8_t raw_prev[32];
    hex_to_bytes(stratum_hex, raw_prev, 32);

    const char *merkle_display =
        "2b12fcf1b09288fcaff797d71e950e71ae42b91e8bdb2304758dfcffc2b620e3";
    uint8_t merkle_root[32];
    hex_to_bytes(merkle_display, merkle_root, 32);
    reverse_bytes(merkle_root, 32);

    uint8_t header[80];
    build_block_header(header, 1, raw_prev, merkle_root,
                       0x4dd7f5c7, 0x1a44b9f2, 2504433986);

    uint8_t hash[32];
    double_sha256(header, 80, hash);

    double diff = difficulty_from_hash(hash);
    printf("  Difficulty WITHOUT swap: %.6f (should be garbage)\n", diff);
    TEST_ASSERT(diff < 10.0,
                "without swap_endian_words_bin, difficulty is garbage");
}

/* ================================================================
 * Test 5: Full simulation of ASIC result processing
 * ================================================================
 *
 * Simulates the complete flow matching forge-os:
 * 1. Stratum notify provides prev_block_hash (wire format)
 * 2. stratum_build_asic_job converts it (with swap_endian_words_bin)
 * 3. ASIC returns nonce + rolled_version
 * 4. result_task builds header and computes difficulty
 */
static void test_full_asic_flow(void)
{
    printf("\n=== Test 5: Full ASIC result simulation ===\n");

    /* Step 1: Stratum provides prev_hash in wire format */
    const char *stratum_prev_hex =
        "ab02cd818b9e567ee21793cddef299feb29ad444a41b85b8000008a300000000";
    uint8_t stratum_prev[32];
    hex_to_bytes(stratum_prev_hex, stratum_prev, 32);

    /* Step 2: Convert to internal byte order (THE FIX) */
    uint8_t prev_hash[32];
    swap_endian_words_bin(stratum_prev, prev_hash, 32);

    /* Merkle root from mining_compute_merkle_root (already in internal order) */
    const char *merkle_display =
        "2b12fcf1b09288fcaff797d71e950e71ae42b91e8bdb2304758dfcffc2b620e3";
    uint8_t merkle_root[32];
    hex_to_bytes(merkle_display, merkle_root, 32);
    reverse_bytes(merkle_root, 32);

    /* Job fields (as stored in asic_job_t) */
    uint32_t version = 1;
    uint32_t ntime   = 0x4dd7f5c7;
    uint32_t nbits   = 0x1a44b9f2;

    /* Step 3: ASIC returns nonce (no version rolling for simplicity) */
    uint32_t nonce = 2504433986;
    uint32_t rolled_version = version;  /* no rolling */

    /* Step 4: Build header and test (matching result_task.c logic) */
    uint8_t header[80];
    memcpy(header,      &rolled_version, 4);
    memcpy(header + 4,  prev_hash, 32);
    memcpy(header + 36, merkle_root, 32);
    memcpy(header + 68, &ntime, 4);
    memcpy(header + 72, &nbits, 4);
    memcpy(header + 76, &nonce, 4);

    uint8_t hash[32];
    double_sha256(header, 80, hash);

    double diff = difficulty_from_hash(hash);
    printf("  Difficulty from full flow: %.4f\n", diff);
    TEST_ASSERT(diff > 200000,
                "full ASIC flow produces correct difficulty (~244,112)");

    /* Verify the exact hash */
    const char *expected_hash_display =
        "00000000000000001e8d6829a8a21adc5d38d0a473b144b6765798e61f98bd1d";
    uint8_t expected[32];
    hex_to_bytes(expected_hash_display, expected, 32);
    reverse_bytes(expected, 32);

    TEST_ASSERT(memcmp(hash, expected, 32) == 0,
                "full ASIC flow produces correct block hash");
}

/* ================================================================ */
int main(void)
{
    printf("Nonce Verification Tests\n");
    printf("========================\n");

    test_genesis_block();
    test_block_125552();
    test_swap_endian_words();
    test_bug_without_swap();
    test_full_asic_flow();

    printf("\n========================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
