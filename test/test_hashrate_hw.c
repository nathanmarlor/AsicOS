#include <stdio.h>
#include <math.h>
#include <stdint.h>

/*
 * Test the hardware counter hashrate formula (forge-os):
 *   GH/s = counter_delta / dt_seconds * 2^32 / 1e9
 *
 * At 500MHz with 2040 small cores per chip:
 *   Expected per-chip: 500e6 * 2040 / 1e9 = 1020 GH/s
 *   Over 10 seconds: counter_delta = 1020e9 * 10 / 4294967296 = ~2375
 */

int main(void)
{
    int pass = 1;

    /* Test 1: Single chip at 500 MHz / 2040 cores */
    {
        uint32_t counter_delta = 2375;
        double dt_seconds = 10.0;
        double ghs = (double)counter_delta / dt_seconds * 4294967296.0 / 1e9;

        printf("Test 1: counter_delta=%u, dt=%.1fs\n", counter_delta, dt_seconds);
        printf("  GH/s = %.2f (expected ~1020)\n", ghs);

        if (fabs(ghs - 1020.0) > 50.0) {
            printf("  FAIL\n");
            pass = 0;
        } else {
            printf("  PASS\n");
        }
    }

    /* Test 2: Two chips aggregated */
    {
        uint32_t delta_chip0 = 2375;
        uint32_t delta_chip1 = 2380;
        double dt_seconds = 10.0;
        double ghs0 = (double)delta_chip0 / dt_seconds * 4294967296.0 / 1e9;
        double ghs1 = (double)delta_chip1 / dt_seconds * 4294967296.0 / 1e9;
        double total = ghs0 + ghs1;

        printf("Test 2: two chips, total GH/s = %.2f (expected ~2040)\n", total);

        if (fabs(total - 2040.0) > 100.0) {
            printf("  FAIL\n");
            pass = 0;
        } else {
            printf("  PASS\n");
        }
    }

    /* Test 3: Counter wrap (uint32 overflow) */
    {
        uint32_t prev = 0xFFFFFF00;
        uint32_t curr = 0x00000100;
        uint32_t delta = curr - prev;  /* should be 0x200 = 512 */
        double dt_seconds = 10.0;
        double ghs = (double)delta / dt_seconds * 4294967296.0 / 1e9;

        printf("Test 3: counter wrap, delta=%u, GH/s = %.2f\n", delta, ghs);

        if (delta != 0x200) {
            printf("  FAIL: wrong delta\n");
            pass = 0;
        } else {
            printf("  PASS\n");
        }
    }

    /* Test 4: Zero delta => zero hashrate */
    {
        uint32_t delta = 0;
        double dt_seconds = 10.0;
        double ghs = (double)delta / dt_seconds * 4294967296.0 / 1e9;

        printf("Test 4: zero delta, GH/s = %.2f (expected 0)\n", ghs);

        if (ghs != 0.0) {
            printf("  FAIL\n");
            pass = 0;
        } else {
            printf("  PASS\n");
        }
    }

    printf("\n%s\n", pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return pass ? 0 : 1;
}
