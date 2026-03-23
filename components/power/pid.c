#include "pid.h"

void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float output_min, float output_max)
{
    pid->kp         = kp;
    pid->ki         = ki;
    pid->kd         = kd;
    pid->target     = 0.0f;
    pid->output_min = output_min;
    pid->output_max = output_max;
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    pid->first_run  = true;
}

void pid_set_target(pid_controller_t *pid, float target)
{
    pid->target = target;
}

float pid_compute(pid_controller_t *pid, float input, float dt)
{
    /* Positive error = input above target (e.g. too hot -> more fan) */
    float error = input - pid->target;

    /* Proportional */
    float p_term = pid->kp * error;

    /* Integral with anti-windup */
    pid->integral += error * dt;
    float i_term = pid->ki * pid->integral;

    /* Clamp integral to prevent windup */
    if (i_term > pid->output_max) {
        i_term = pid->output_max;
        pid->integral = pid->output_max / pid->ki;
    } else if (i_term < pid->output_min) {
        i_term = pid->output_min;
        pid->integral = pid->output_min / pid->ki;
    }

    /* Derivative (skip on first run to avoid spike) */
    float d_term = 0.0f;
    if (!pid->first_run && dt > 0.0f) {
        d_term = pid->kd * (error - pid->prev_error) / dt;
    }
    pid->prev_error = error;
    pid->first_run  = false;

    /* Sum and clamp output */
    float output = p_term + i_term + d_term;
    if (output > pid->output_max) {
        output = pid->output_max;
    } else if (output < pid->output_min) {
        output = pid->output_min;
    }

    return output;
}

void pid_reset(pid_controller_t *pid)
{
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    pid->first_run  = true;
}
