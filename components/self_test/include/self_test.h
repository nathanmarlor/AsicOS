#pragma once
#include <stdbool.h>

#define SELFTEST_MAX_CHECKS 10

typedef enum {
    SELFTEST_PASS,
    SELFTEST_FAIL,
    SELFTEST_WARN,
    SELFTEST_SKIP,
} selftest_result_t;

typedef struct {
    const char       *name;
    selftest_result_t result;
    char              detail[64];
} selftest_check_t;

typedef struct {
    selftest_check_t checks[SELFTEST_MAX_CHECKS];
    int              check_count;
    bool             all_pass;
} selftest_report_t;

selftest_report_t selftest_run(void);
