#pragma once

#include <stdint.h>

#define RESULT_TASK_STACK_SIZE  8192
#define RESULT_TASK_PRIORITY    15

typedef struct {
    double   session_best_diff;   // Best difficulty this session (since boot)
    double   alltime_best_diff;   // Best difficulty ever (persisted to NVS)
    uint64_t total_shares_submitted;
    uint64_t duplicate_nonces;
} mining_stats_t;

void result_task_start(void);
const mining_stats_t *result_task_get_stats(void);
uint64_t result_task_get_nonce_count(void);
uint64_t result_task_get_chip_nonce_count(int chip);
uint32_t result_task_get_chip_rolling_nonces(int chip);
uint64_t result_task_get_chip_hw_errors(int chip);
uint64_t result_task_get_total_hw_errors(void);
float    result_task_get_hw_error_rate(void);
double   result_task_get_last_share_diff(void);
float    result_task_get_share_rate(void);
