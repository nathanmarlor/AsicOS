#pragma once

#include "stratum_api.h"
#include "asic.h"

#define MINING_TASK_STACK_SIZE  8192
#define MINING_TASK_PRIORITY    10

void mining_task_start(void);

// Callbacks for stratum client
void mining_on_notify(const stratum_notify_t *notify, int pool_id);
void mining_on_difficulty(double diff, int pool_id);

// Job lookup (used by result task)
const asic_job_t *mining_get_job(uint8_t job_id);
