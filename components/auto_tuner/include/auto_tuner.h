#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TUNER_MAX_RESULTS 64

typedef struct {
    uint16_t freq;
    uint16_t voltage;
    float    hashrate_ghs;
    float    power_w;
    float    temp;
    float    efficiency_ghs_per_w;
    bool     stable;
} tuner_result_t;

typedef enum {
    TUNER_STATE_IDLE,
    TUNER_STATE_RUNNING,
    TUNER_STATE_COMPLETE,
    TUNER_STATE_ABORTED,
} tuner_state_t;

typedef struct {
    tuner_state_t state;
    int           total_steps;
    int           current_step;
    tuner_result_t results[TUNER_MAX_RESULTS];
    int           result_count;
    int           best_index;
    int           best_eff_index;
} tuner_status_t;

// Scoring (higher = better)
double tuner_score(const tuner_result_t *r);

// Status access
const tuner_status_t *tuner_get_status(void);

// Internal setters (used by tuner task)
void tuner_set_state(tuner_state_t state);
void tuner_reset_status(void);
tuner_result_t *tuner_get_result_slot(int index);
void tuner_set_result_count(int count);
void tuner_set_step(int current, int total);
void tuner_set_best(int best_idx, int best_eff_idx);
