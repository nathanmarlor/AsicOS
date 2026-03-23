#pragma once

#include <stdint.h>

#define RESULT_TASK_STACK_SIZE  8192
#define RESULT_TASK_PRIORITY    15

typedef struct {
    double   best_difficulty;
    uint64_t total_shares_submitted;
    uint64_t duplicate_nonces;
} mining_stats_t;

void result_task_start(void);
const mining_stats_t *result_task_get_stats(void);
uint64_t result_task_get_nonce_count(void);
uint64_t result_task_get_chip_nonce_count(int chip);
