/*
 * Reproduce the diff=0.0 bug: s_last_valid_diff resets to 0 despite
 * valid shares being processed. Tests the exact code logic from
 * result_task.c without ESP-IDF dependencies.
 *
 * Compile: gcc -Wall -Wextra -o test_diff_zero_bug test_diff_zero_bug.c -lm
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

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

/* ── Simulate the result_task logic exactly ── */

static double s_last_valid_diff = 0.0;
static uint64_t s_total_hw_errors = 0;

/* Simulate processing a nonce with a given share_diff */
static void process_nonce(double share_diff)
{
    if (!isfinite(share_diff) || share_diff <= 0.0) {
        s_total_hw_errors++;
        share_diff = 0.0;
    } else {
        s_last_valid_diff = share_diff;
    }
}

/* ── Tests ── */

static void test_basic_flow(void)
{
    printf("\n=== basic flow: valid nonces set s_last_valid_diff ===\n");
    s_last_valid_diff = 0.0;
    s_total_hw_errors = 0;

    process_nonce(500.0);
    TEST_ASSERT(s_last_valid_diff == 500.0, "after diff=500, last_valid=500");

    process_nonce(300.0);
    TEST_ASSERT(s_last_valid_diff == 300.0, "after diff=300, last_valid=300");

    process_nonce(1000.0);
    TEST_ASSERT(s_last_valid_diff == 1000.0, "after diff=1000, last_valid=1000");

    TEST_ASSERT(s_total_hw_errors == 0, "no hw errors for valid nonces");
}

static void test_nan_caught(void)
{
    printf("\n=== NaN is caught by isfinite ===\n");
    s_last_valid_diff = 500.0;
    s_total_hw_errors = 0;

    process_nonce(NAN);
    TEST_ASSERT(s_last_valid_diff == 500.0, "NaN doesn't overwrite last_valid");
    TEST_ASSERT(s_total_hw_errors == 1, "NaN counted as hw error");
}

static void test_inf_caught(void)
{
    printf("\n=== Inf is caught by isfinite ===\n");
    s_last_valid_diff = 500.0;
    s_total_hw_errors = 0;

    process_nonce(INFINITY);
    TEST_ASSERT(s_last_valid_diff == 500.0, "Inf doesn't overwrite last_valid");
    TEST_ASSERT(s_total_hw_errors == 1, "Inf counted as hw error");
}

static void test_zero_caught(void)
{
    printf("\n=== 0.0 is caught by <= 0 ===\n");
    s_last_valid_diff = 500.0;
    s_total_hw_errors = 0;

    process_nonce(0.0);
    TEST_ASSERT(s_last_valid_diff == 500.0, "0.0 doesn't overwrite last_valid");
    TEST_ASSERT(s_total_hw_errors == 1, "0.0 counted as hw error");
}

static void test_negative_caught(void)
{
    printf("\n=== negative is caught ===\n");
    s_last_valid_diff = 500.0;
    s_total_hw_errors = 0;

    process_nonce(-1.0);
    TEST_ASSERT(s_last_valid_diff == 500.0, "-1.0 doesn't overwrite last_valid");
    TEST_ASSERT(s_total_hw_errors == 1, "-1.0 counted as hw error");
}

static void test_denormalized_double(void)
{
    printf("\n=== denormalized (subnormal) doubles ===\n");
    s_last_valid_diff = 500.0;
    s_total_hw_errors = 0;

    /* Smallest positive subnormal double */
    double subnormal = 5e-324;
    printf("  subnormal = %e, isfinite=%d, >0=%d\n", subnormal, isfinite(subnormal), subnormal > 0.0);
    process_nonce(subnormal);
    TEST_ASSERT(s_last_valid_diff == subnormal, "subnormal is valid (finite & > 0)");
    TEST_ASSERT(s_total_hw_errors == 0, "subnormal not an error");
}

static void test_very_small_positive(void)
{
    printf("\n=== very small positive doubles ===\n");
    s_last_valid_diff = 0.0;
    s_total_hw_errors = 0;

    /* This is what mining_difficulty_from_hash returns for a very bad hash */
    double tiny = 1e-300;
    printf("  tiny = %e, isfinite=%d, >0=%d, <=0=%d\n",
           tiny, isfinite(tiny), tiny > 0.0, tiny <= 0.0);
    process_nonce(tiny);
    TEST_ASSERT(s_last_valid_diff == tiny, "1e-300 sets last_valid");
    TEST_ASSERT(s_total_hw_errors == 0, "1e-300 not an error");

    /* Check what printf does with it */
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f", tiny);
    printf("  printf('%%0.1f', 1e-300) = '%s'\n", buf);
    snprintf(buf, sizeof(buf), "%.4f", tiny);
    printf("  printf('%%0.4f', 1e-300) = '%s'\n", buf);

    /* AH HA! */
    TEST_ASSERT(strcmp(buf, "0.0000") == 0,
                "1e-300 prints as 0.0000 with %%0.4f - THIS IS THE BUG!");
}

static void test_printf_display_bug(void)
{
    printf("\n=== THE BUG: valid positive doubles print as 0.0 ===\n");

    /* mining_difficulty_from_hash returns TRUEDIFFONE / le256todouble(hash).
     * For a hash where le256todouble is very large (~1e77), the result is
     * TRUEDIFFONE(~2.7e67) / 1e77 = ~2.7e-10. This IS finite and > 0,
     * so it passes the guards. But %.1f displays it as "0.0". */

    double values[] = { 1e-1, 1e-5, 1e-10, 2.3e-10, 1e-15, 1e-100, 1e-300 };
    int n = sizeof(values) / sizeof(values[0]);

    printf("  %-15s %-10s %-10s %-10s %-8s\n", "value", "%.1f", "%.4f", "%.4e", ">0?");
    for (int i = 0; i < n; i++) {
        char f1[32], f4[32], fe[32];
        snprintf(f1, sizeof(f1), "%.1f", values[i]);
        snprintf(f4, sizeof(f4), "%.4f", values[i]);
        snprintf(fe, sizeof(fe), "%.4e", values[i]);
        printf("  %-15e %-10s %-10s %-10s %-8s\n",
               values[i], f1, f4, fe, values[i] > 0 ? "yes" : "no");
    }

    /* The key insight: ANY difficulty < 0.05 will print as "0.0" with %.1f
     * and ANY difficulty < 0.00005 will print as "0.0000" with %.4f.
     * These are valid finite positive numbers that pass all guards,
     * but LOOK like zero in the logs. */

    double bug_value = 2.3e-10;
    TEST_ASSERT(isfinite(bug_value), "2.3e-10 is finite");
    TEST_ASSERT(bug_value > 0.0, "2.3e-10 is > 0");
    TEST_ASSERT(!(bug_value <= 0.0), "2.3e-10 is NOT <= 0");

    char display[32];
    snprintf(display, sizeof(display), "%.1f", bug_value);
    TEST_ASSERT(strcmp(display, "0.0") == 0,
                "2.3e-10 displays as '0.0' with %.1f - PRINTF ROUNDING!");

    printf("\n  ROOT CAUSE: mining_difficulty_from_hash returns tiny positive\n"
           "  values (e.g. 2.3e-10) for hashes where all 32 bytes are large.\n"
           "  These are valid finite positive doubles that pass isfinite()\n"
           "  and > 0 checks, but printf with %%.1f rounds them to '0.0'.\n"
           "  s_last_valid_diff IS being set correctly - it's just too small\n"
           "  to display with fixed-point formatting.\n");
}

static void test_simulated_mining_sequence(void)
{
    printf("\n=== simulated mining: reproduce the exact bug pattern ===\n");
    s_last_valid_diff = 0.0;
    s_total_hw_errors = 0;

    /* Most nonces from BM1370 at ASIC difficulty 256 produce hash values
     * where the first ~1 byte is zero (LE), giving difficulties of ~256-1000.
     * But some nonces produce hashes with NO leading zeros, giving
     * difficulties like 0.001 or 1e-10 which display as "0.0". */

    double nonce_diffs[] = {
        500.0,      /* normal nonce */
        300.0,      /* normal nonce */
        2.3e-10,    /* hash with no leading zeros - displays as 0.0! */
        700.0,      /* normal nonce */
        1.5e-8,     /* another "bad" hash - displays as 0.0! */
        400.0,      /* normal nonce */
    };

    for (int i = 0; i < 6; i++) {
        process_nonce(nonce_diffs[i]);
        char display[32];
        snprintf(display, sizeof(display), "%.1f", s_last_valid_diff);
        printf("  nonce diff=%.4e -> s_last_valid=%.4e (displays as %s) err=%llu\n",
               nonce_diffs[i], s_last_valid_diff, display,
               (unsigned long long)s_total_hw_errors);
    }

    TEST_ASSERT(s_total_hw_errors == 0, "all nonces are valid (no hw errors)");
    TEST_ASSERT(s_last_valid_diff == 400.0, "last_valid set to last nonce");

    /* But if the summary fires on the 2.3e-10 nonce: */
    s_last_valid_diff = 2.3e-10;
    char display[32];
    snprintf(display, sizeof(display), "%.1f", s_last_valid_diff);
    TEST_ASSERT(strcmp(display, "0.0") == 0,
                "summary would show diff=0.0 even though it's valid!");
}

int main(void)
{
    printf("Diff=0.0 Bug Deep Dive\n");
    printf("======================\n");

    test_basic_flow();
    test_nan_caught();
    test_inf_caught();
    test_zero_caught();
    test_negative_caught();
    test_denormalized_double();
    test_very_small_positive();
    test_printf_display_bug();
    test_simulated_mining_sequence();

    printf("\n======================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
