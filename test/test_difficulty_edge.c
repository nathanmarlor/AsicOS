/*
 * Edge-case tests for difficulty calculation and share validation.
 * Tests NaN, Inf, zero, and boundary conditions in le256todouble
 * and mining_difficulty_from_hash to catch the diff=0.0 bug.
 *
 * Compile: gcc -Wall -Wextra -o test_difficulty_edge test_difficulty_edge.c -lm
 * Run:     ./test_difficulty_edge
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>

/* ── Re-implement mining functions for host testing ── */

static const double TRUEDIFFONE =
    26959535291011309493156476344723991336010898738574164086137773096960.0;
static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
static const double bits128 = 340282366920938463463374607431768211456.0;
static const double bits64  = 18446744073709551616.0;

static double le256todouble(const void *target)
{
    uint64_t d;
    double dcut64;

    memcpy(&d, (const uint8_t *)target + 24, sizeof(d));
    dcut64 = d * bits192;
    memcpy(&d, (const uint8_t *)target + 16, sizeof(d));
    dcut64 += d * bits128;
    memcpy(&d, (const uint8_t *)target + 8, sizeof(d));
    dcut64 += d * bits64;
    memcpy(&d, (const uint8_t *)target, sizeof(d));
    dcut64 += d;

    return dcut64;
}

static double mining_difficulty_from_hash(const uint8_t *hash)
{
    double d64 = TRUEDIFFONE;
    double s64 = le256todouble(hash);
    double ds = d64 / s64;
    return ds;
}

/* ── Test framework ── */

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

/* ── Tests ── */

static void test_all_zeros_hash(void)
{
    printf("\n=== difficulty: all-zeros hash ===\n");
    uint8_t hash[32] = {0};
    double s64 = le256todouble(hash);
    double diff = mining_difficulty_from_hash(hash);
    printf("  le256todouble = %e\n", s64);
    printf("  difficulty    = %e\n", diff);
    printf("  isfinite      = %d\n", isfinite(diff));
    printf("  isnan         = %d\n", isnan(diff));
    printf("  isinf         = %d\n", isinf(diff));

    /* All-zeros hash: le256todouble returns 0, TRUEDIFFONE/0 = +Inf */
    TEST_ASSERT(s64 == 0.0, "le256todouble of all-zeros is 0");
    TEST_ASSERT(isinf(diff), "TRUEDIFFONE / 0 is Inf");
    TEST_ASSERT(!isfinite(diff), "Inf is not finite");
    TEST_ASSERT(!(diff <= 0.0), "Inf is NOT <= 0 (would bypass HW error check!)");
    TEST_ASSERT(!(diff > 0.0 && isfinite(diff)), "Inf is not a valid positive finite diff");
}

static void test_all_ff_hash(void)
{
    printf("\n=== difficulty: all-0xFF hash ===\n");
    uint8_t hash[32];
    memset(hash, 0xFF, 32);
    double s64 = le256todouble(hash);
    double diff = mining_difficulty_from_hash(hash);
    printf("  le256todouble = %e\n", s64);
    printf("  difficulty    = %e\n", diff);
    printf("  isfinite      = %d\n", isfinite(diff));

    /* Max hash: le256todouble returns a huge value, diff should be very small but > 0 */
    TEST_ASSERT(s64 > 0, "le256todouble of all-FF is positive");
    TEST_ASSERT(isfinite(diff), "difficulty of all-FF hash is finite");
    TEST_ASSERT(diff > 0.0, "difficulty of all-FF hash is positive");
    TEST_ASSERT(diff < 1.0, "difficulty of all-FF hash is < 1 (poor hash)");
}

static void test_single_leading_zero(void)
{
    printf("\n=== difficulty: hash with one leading zero byte ===\n");
    uint8_t hash[32];
    memset(hash, 0xFF, 32);
    hash[31] = 0x00;  /* LE: MSB is at end */
    double diff = mining_difficulty_from_hash(hash);
    printf("  difficulty = %e\n", diff);
    TEST_ASSERT(isfinite(diff), "finite");
    TEST_ASSERT(diff > 0.0, "positive");
}

static void test_difficulty_1_target(void)
{
    printf("\n=== difficulty: difficulty-1 target hash ===\n");
    /* Difficulty 1 target = 0x00000000FFFF... (big endian)
     * In LE (as stored in hash): bytes 28-31 = 0x00, byte 27 = 0xFF, byte 26 = 0xFF */
    uint8_t hash[32] = {0};
    hash[28] = 0xFF;
    hash[29] = 0xFF;
    /* This approximates a diff-1 hash */
    double diff = mining_difficulty_from_hash(hash);
    printf("  difficulty = %e\n", diff);
    TEST_ASSERT(isfinite(diff), "finite");
    TEST_ASSERT(diff > 0.0, "positive difficulty for diff-1-ish hash");
}

static void test_typical_asic_diff(void)
{
    printf("\n=== difficulty: typical ASIC difficulty 256 hash ===\n");
    /* A hash at difficulty ~256 has about 1 leading zero byte at the end (LE) */
    uint8_t hash[32];
    memset(hash, 0x42, 32);
    hash[31] = 0x00;
    hash[30] = 0x00;
    double diff = mining_difficulty_from_hash(hash);
    printf("  difficulty = %e\n", diff);
    TEST_ASSERT(isfinite(diff), "finite");
    TEST_ASSERT(diff > 0.0, "positive");
    TEST_ASSERT(diff > 0.0, "positive for hash with 2 zero MSB bytes");
}

static void test_le256todouble_overflow(void)
{
    printf("\n=== le256todouble: large values don't overflow to Inf ===\n");
    uint8_t hash[32];
    memset(hash, 0xFF, 32);
    double val = le256todouble(hash);
    printf("  value    = %e\n", val);
    printf("  isfinite = %d\n", isfinite(val));
    /* 2^256 - 1 should be representable as a double (imprecise but finite) */
    TEST_ASSERT(isfinite(val), "max 256-bit value is finite as double");
    TEST_ASSERT(val > 0, "positive");
}

static void test_share_diff_check_logic(void)
{
    printf("\n=== share_diff check: NaN bypasses <= 0 and > 0 ===\n");
    double nan_val = NAN;
    double inf_val = INFINITY;
    double neg_inf = -INFINITY;
    double zero = 0.0;
    double valid = 300.5;

    /* This is the bug: NaN bypasses both checks */
    TEST_ASSERT(!(nan_val <= 0.0), "NaN is NOT <= 0 (bypasses HW error check!)");
    TEST_ASSERT(!(nan_val > 0.0), "NaN is NOT > 0 (bypasses valid diff set!)");
    TEST_ASSERT(!isfinite(nan_val), "NaN caught by isfinite");

    TEST_ASSERT(!(inf_val <= 0.0), "Inf is NOT <= 0");
    TEST_ASSERT(inf_val > 0.0, "Inf IS > 0 (would set s_last_valid_diff to Inf!)");
    TEST_ASSERT(!isfinite(inf_val), "Inf caught by isfinite");

    TEST_ASSERT(neg_inf <= 0.0, "-Inf IS <= 0 (caught by normal check)");
    TEST_ASSERT(zero <= 0.0, "0.0 IS <= 0 (caught by normal check)");
    TEST_ASSERT(valid > 0.0, "300.5 IS > 0");
    TEST_ASSERT(isfinite(valid), "300.5 is finite");

    /* Correct check: !isfinite(x) || x <= 0.0 */
    printf("\n  Correct guard: !isfinite(x) || x <= 0.0\n");
    TEST_ASSERT(!isfinite(nan_val) || nan_val <= 0.0, "NaN caught by isfinite guard");
    TEST_ASSERT(!isfinite(inf_val) || inf_val <= 0.0, "Inf caught by isfinite guard");
    TEST_ASSERT(!isfinite(neg_inf) || neg_inf <= 0.0, "-Inf caught by either guard");
    TEST_ASSERT(!isfinite(zero) || zero <= 0.0, "0.0 caught by <= 0 guard");
    TEST_ASSERT(isfinite(valid) && valid > 0.0, "300.5 passes both guards");
}

static void test_random_hashes_always_positive(void)
{
    printf("\n=== difficulty: 1000 random-ish hashes all produce finite positive diff ===\n");
    int failures = 0;
    for (int i = 0; i < 1000; i++) {
        uint8_t hash[32];
        /* Fill with pseudo-random pattern */
        for (int j = 0; j < 32; j++) {
            hash[j] = (uint8_t)((i * 7 + j * 13 + 37) & 0xFF);
        }
        /* Ensure not all zeros (which gives Inf) */
        hash[0] |= 0x01;

        double diff = mining_difficulty_from_hash(hash);
        if (!isfinite(diff) || diff <= 0.0) {
            printf("  FAIL at i=%d: diff=%e\n", i, diff);
            failures++;
        }
    }
    TEST_ASSERT(failures == 0, "all 1000 non-zero hashes produce finite positive difficulty");
}

static void test_all_zeros_except_lsb(void)
{
    printf("\n=== difficulty: hash = 0x01 followed by 31 zeros ===\n");
    uint8_t hash[32] = {0};
    hash[0] = 0x01;  /* smallest non-zero LE value */
    double s64 = le256todouble(hash);
    double diff = mining_difficulty_from_hash(hash);
    printf("  le256todouble = %e\n", s64);
    printf("  difficulty    = %e\n", diff);
    TEST_ASSERT(s64 == 1.0, "le256todouble of 1 is 1.0");
    TEST_ASSERT(diff == TRUEDIFFONE, "difficulty equals TRUEDIFFONE");
    TEST_ASSERT(isfinite(diff), "finite");
}

/* ── Main ── */

int main(void)
{
    printf("Difficulty Edge Case Tests\n");
    printf("==========================\n");

    test_all_zeros_hash();
    test_all_ff_hash();
    test_single_leading_zero();
    test_difficulty_1_target();
    test_typical_asic_diff();
    test_le256todouble_overflow();
    test_share_diff_check_logic();
    test_random_hashes_always_positive();
    test_all_zeros_except_lsb();

    printf("\n==========================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
