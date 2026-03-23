#pragma once

#include <stdint.h>
#include <stdbool.h>

#define POWER_TASK_STACK_SIZE 8192
#define POWER_TASK_PRIORITY   10

typedef struct {
    float chip_temp;
    float vr_temp;
    float board_temp;
    float vin;
    float vout;
    float power_w;
    uint16_t fan0_rpm;
    uint16_t fan1_rpm;
    bool  overheat;
    bool  vr_fault;
} power_status_t;

void power_task_start(void);
const power_status_t *power_task_get_status(void);
