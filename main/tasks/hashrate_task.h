#pragma once

#include <stdint.h>

#define HASHRATE_TASK_STACK_SIZE 4096
#define HASHRATE_TASK_PRIORITY   10

#define HASHRATE_NUM_DOMAINS 4

typedef struct {
    float total_hashrate_ghs;
    float per_chip_hashrate_ghs[16];
    float per_domain_hashrate_ghs[16][HASHRATE_NUM_DOMAINS];
    int   chip_count;
} hashrate_info_t;

void hashrate_task_start(void);
const hashrate_info_t *hashrate_task_get_info(void);
