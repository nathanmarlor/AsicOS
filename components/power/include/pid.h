#pragma once
#include <stdbool.h>

typedef struct {
    float kp;
    float ki;
    float kd;
    float target;
    float output_min;
    float output_max;
    float integral;
    float prev_error;
    bool  first_run;
} pid_controller_t;

void  pid_init(pid_controller_t *pid, float kp, float ki, float kd,
               float output_min, float output_max);
void  pid_set_target(pid_controller_t *pid, float target);
float pid_compute(pid_controller_t *pid, float input, float dt);
void  pid_reset(pid_controller_t *pid);
