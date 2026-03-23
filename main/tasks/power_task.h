#pragma once

#include <stdint.h>
#include <stdbool.h>

#define POWER_TASK_STACK_SIZE 8192
#define POWER_TASK_PRIORITY   10

typedef struct {
    float chip_temp;
    float vr_temp;
    float board_temp;
    float therm1_temp;   // ADC thermistor ASIC 1 (if available)
    float therm2_temp;   // ADC thermistor ASIC 2 (if available)
    float vin;
    float vout;
    float iout;          // Output current from VR telemetry (A)
    float vcore_adc_mv;  // ADC VCORE readback (if available)
    float power_w;
    float iin;           // Input current (A) - from INA260 or VR telemetry
    float input_w;       // Input power: vin * iin (W)
    uint16_t fan0_rpm;
    uint16_t fan1_rpm;
    bool  overheat;
    bool  vr_fault;
} power_status_t;

void power_task_start(void);
const power_status_t *power_task_get_status(void);

// Fan manual override: set to 0-100 for manual %, or -1 to return to PID auto
void power_set_fan_override(int percent);
int power_get_fan_override(void);
