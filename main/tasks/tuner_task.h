#pragma once
#include <stdint.h>

#define TUNER_TASK_STACK_SIZE 8192
#define TUNER_TASK_PRIORITY   5

void tuner_task_start(void);
void tuner_start(uint16_t freq_min, uint16_t freq_max, uint16_t freq_step,
                 uint16_t volt_min, uint16_t volt_max, uint16_t volt_step);
void tuner_abort(void);
void tuner_apply_best(void);
