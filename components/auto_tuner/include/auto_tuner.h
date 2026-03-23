#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TUNER_MAX_RESULTS 64

typedef enum {
    TUNER_MODE_ECO,       // Optimise for GH/s per watt
    TUNER_MODE_BALANCED,  // Best overall score (hashrate * efficiency * thermal)
    TUNER_MODE_POWER,     // Maximum hashrate regardless of power
} tuner_mode_t;

typedef struct {
    uint16_t freq;
    uint16_t voltage;
    float    hashrate_ghs;
    float    power_w;
    float    temp;
    float    efficiency_ghs_per_w;
    bool     stable;
} tuner_result_t;

typedef struct {
    tuner_result_t eco;       // Best GH/s per watt
    tuner_result_t balanced;  // Best overall score
    tuner_result_t power;     // Best raw hashrate
    bool eco_valid;
    bool balanced_valid;
    bool power_valid;
} tuner_profiles_t;

typedef enum {
    TUNER_STATE_IDLE,
    TUNER_STATE_RUNNING,
    TUNER_STATE_COMPLETE,
    TUNER_STATE_ABORTED,
} tuner_state_t;

typedef struct {
    tuner_state_t    state;
    int              total_steps;
    int              current_step;
    tuner_result_t   results[TUNER_MAX_RESULTS];
    int              result_count;
    int              best_index;
    int              best_eff_index;
    tuner_profiles_t profiles;
} tuner_status_t;

// Scoring (higher = better) -- mode-dependent
double tuner_score(const tuner_result_t *r, tuner_mode_t mode);

// Status access
const tuner_status_t *tuner_get_status(void);

// Profile access
const tuner_profiles_t *tuner_get_profiles(void);

// Internal setters (used by tuner task)
void tuner_set_state(tuner_state_t state);
void tuner_reset_status(void);
tuner_result_t *tuner_get_result_slot(int index);
void tuner_set_result_count(int count);
void tuner_set_step(int current, int total);
void tuner_set_best(int best_idx, int best_eff_idx);
void tuner_set_profiles(const tuner_profiles_t *p);
