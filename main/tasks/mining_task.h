#pragma once

#include "stratum_api.h"
#include "asic.h"

#define MINING_TASK_STACK_SIZE  8192
#define MINING_TASK_PRIORITY    10

void mining_task_start(void);

// Callbacks for stratum client
void mining_on_notify(const stratum_notify_t *notify, int pool_id);
void mining_on_difficulty(double diff, int pool_id);

// Job lookup (used by result task) - copies job into caller buffer while holding mutex
bool mining_get_job(uint8_t job_id, asic_job_t *out);
