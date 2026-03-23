/*
 * Host-side tests for PID controller.
 *
 * Compile: gcc -Wall -Wextra -I../components/power/include -o test_pid test_pid.c ../components/power/pid.c -lm
 * Run:     ./test_pid
 */
#include <stdio.h>
#include <math.h>
#include "pid.h"

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
 * Test: output increases when input > setpoint
 * ================================================================ */
static void test_output_increases_above_setpoint(void)
{
    printf("\n=== PID: output increases when input > setpoint ===\n");

    pid_controller_t pid;
    pid_init(&pid, 1.0f, 0.1f, 0.0f, 0.0f, 100.0f);
    pid_set_target(&pid, 50.0f);

    /* Input = 60, above target of 50 -> positive error -> positive output */
    float out = pid_compute(&pid, 60.0f, 1.0f);
    printf("  Input=60, target=50, output=%.2f\n", out);
    TEST_ASSERT(out > 0.0f, "output > 0 when input above setpoint");

    /* Higher input should produce higher output */
    pid_reset(&pid);
    float out_high = pid_compute(&pid, 80.0f, 1.0f);
    printf("  Input=80, target=50, output=%.2f\n", out_high);
    TEST_ASSERT(out_high > out, "higher input produces higher output");
}

/* ================================================================
 * Test: output decreases when input < setpoint
 * ================================================================ */
static void test_output_decreases_below_setpoint(void)
{
    printf("\n=== PID: output when input < setpoint ===\n");

    pid_controller_t pid;
    pid_init(&pid, 1.0f, 0.1f, 0.0f, 0.0f, 100.0f);
    pid_set_target(&pid, 50.0f);

    /* Input = 40, below target -> negative error -> output clamped to min */
    float out = pid_compute(&pid, 40.0f, 1.0f);
    printf("  Input=40, target=50, output=%.2f\n", out);
    TEST_ASSERT(out <= 0.0f, "output <= 0 when input below setpoint (clamped to min)");
}

/* ================================================================
 * Test: output stays in bounds (min/max)
 * ================================================================ */
static void test_output_bounds(void)
{
    printf("\n=== PID: output stays in bounds ===\n");

    pid_controller_t pid;
    pid_init(&pid, 10.0f, 1.0f, 0.0f, 0.0f, 100.0f);
    pid_set_target(&pid, 50.0f);

    /* Very high input: should clamp to max */
    float out = pid_compute(&pid, 200.0f, 1.0f);
    printf("  Input=200, max=100, output=%.2f\n", out);
    TEST_ASSERT(out <= 100.0f, "output <= max (100)");

    /* Very low input: should clamp to min */
    pid_reset(&pid);
    out = pid_compute(&pid, -100.0f, 1.0f);
    printf("  Input=-100, min=0, output=%.2f\n", out);
    TEST_ASSERT(out >= 0.0f, "output >= min (0)");
}

static void test_output_bounds_custom(void)
{
    printf("\n=== PID: custom output bounds ===\n");

    pid_controller_t pid;
    pid_init(&pid, 10.0f, 1.0f, 0.0f, 20.0f, 80.0f);
    pid_set_target(&pid, 50.0f);

    /* Very high input */
    float out = pid_compute(&pid, 200.0f, 1.0f);
    printf("  output=%.2f (max=80)\n", out);
    TEST_ASSERT(out <= 80.0f, "output <= custom max (80)");

    /* Very low input */
    pid_reset(&pid);
    out = pid_compute(&pid, -100.0f, 1.0f);
    printf("  output=%.2f (min=20)\n", out);
    TEST_ASSERT(out >= 20.0f, "output >= custom min (20)");
}

/* ================================================================
 * Test: anti-windup works
 * ================================================================ */
static void test_anti_windup(void)
{
    printf("\n=== PID: anti-windup ===\n");

    pid_controller_t pid;
    pid_init(&pid, 0.0f, 1.0f, 0.0f, 0.0f, 100.0f);  /* pure integral */
    pid_set_target(&pid, 50.0f);

    /* Accumulate large positive error for many iterations */
    for (int i = 0; i < 1000; i++) {
        pid_compute(&pid, 200.0f, 1.0f);
    }

    /* Integral term should be clamped, not astronomical */
    float out_after_saturate = pid_compute(&pid, 200.0f, 1.0f);
    printf("  After 1000 iterations of large error: output=%.2f\n", out_after_saturate);
    TEST_ASSERT(out_after_saturate == 100.0f, "output saturated at max");

    /* The integral should have been clamped */
    TEST_ASSERT(pid.integral <= 100.0f, "integral clamped by anti-windup");

    /* Now drop input below target - output should decrease quickly */
    float out = pid_compute(&pid, 10.0f, 1.0f);
    printf("  After switching to input=10: output=%.2f\n", out);
    /* With anti-windup, integral doesn't need to unwind from infinity */
    /* After a few steps of negative error, output should be below max */
    for (int i = 0; i < 5; i++) {
        out = pid_compute(&pid, 10.0f, 1.0f);
    }
    printf("  After 5 more steps at input=10: output=%.2f\n", out);
    TEST_ASSERT(out < 100.0f, "output recovers from saturation within 5 steps");
}

/* ================================================================
 * Test: reset clears state
 * ================================================================ */
static void test_reset(void)
{
    printf("\n=== PID: reset clears state ===\n");

    pid_controller_t pid;
    pid_init(&pid, 1.0f, 1.0f, 1.0f, 0.0f, 100.0f);
    pid_set_target(&pid, 50.0f);

    /* Run a few iterations to accumulate state */
    pid_compute(&pid, 70.0f, 1.0f);
    pid_compute(&pid, 75.0f, 1.0f);
    pid_compute(&pid, 80.0f, 1.0f);

    TEST_ASSERT(pid.integral != 0.0f, "integral is non-zero before reset");
    TEST_ASSERT(pid.prev_error != 0.0f, "prev_error is non-zero before reset");
    TEST_ASSERT(pid.first_run == false, "first_run is false before reset");

    pid_reset(&pid);

    TEST_ASSERT(pid.integral == 0.0f, "integral is zero after reset");
    TEST_ASSERT(pid.prev_error == 0.0f, "prev_error is zero after reset");
    TEST_ASSERT(pid.first_run == true, "first_run is true after reset");

    /* Target should be preserved */
    TEST_ASSERT(pid.target == 50.0f, "target preserved after reset");
    /* Gains should be preserved */
    TEST_ASSERT(pid.kp == 1.0f, "kp preserved after reset");
}

/* ================================================================
 * Test: derivative skip on first run
 * ================================================================ */
static void test_derivative_first_run(void)
{
    printf("\n=== PID: derivative skipped on first run ===\n");

    pid_controller_t pid;
    /* Large kd, zero kp/ki to isolate derivative term */
    pid_init(&pid, 0.0f, 0.0f, 100.0f, -1000.0f, 1000.0f);
    pid_set_target(&pid, 0.0f);

    /* First compute should skip derivative (no spike) */
    float out = pid_compute(&pid, 50.0f, 1.0f);
    printf("  First run output (large error, large kd): %.2f\n", out);
    TEST_ASSERT(fabsf(out) < 0.01f, "no derivative spike on first run");

    /* Second compute should include derivative */
    float out2 = pid_compute(&pid, 100.0f, 1.0f);
    printf("  Second run output (error changed): %.2f\n", out2);
    TEST_ASSERT(fabsf(out2) > 0.0f, "derivative active on second run");
}

/* ================================================================
 * Test: zero dt doesn't cause division by zero
 * ================================================================ */
static void test_zero_dt(void)
{
    printf("\n=== PID: zero dt safety ===\n");

    pid_controller_t pid;
    pid_init(&pid, 1.0f, 1.0f, 1.0f, 0.0f, 100.0f);
    pid_set_target(&pid, 50.0f);

    /* First run to clear first_run flag */
    pid_compute(&pid, 60.0f, 1.0f);

    /* Zero dt should not crash (derivative is guarded) */
    float out = pid_compute(&pid, 70.0f, 0.0f);
    printf("  Output with dt=0: %.2f\n", out);
    TEST_ASSERT(isfinite(out), "output is finite with dt=0");
}

/* ================================================================ */
int main(void)
{
    printf("PID Controller Tests\n");
    printf("====================\n");

    test_output_increases_above_setpoint();
    test_output_decreases_below_setpoint();
    test_output_bounds();
    test_output_bounds_custom();
    test_anti_windup();
    test_reset();
    test_derivative_first_run();
    test_zero_dt();

    printf("\n====================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
