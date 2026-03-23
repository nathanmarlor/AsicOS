#include "auto_tuner.h"
#include <string.h>

static tuner_status_t s_status;

double tuner_score(const tuner_result_t *r)
{
    if (!r->stable) {
        return 0.0;
    }

    /* Temp penalty: above 60C ramp down linearly, above 70C = 0 */
    double temp_penalty = 1.0;
    if (r->temp >= 70.0f) {
        temp_penalty = 0.0;
    } else if (r->temp > 60.0f) {
        temp_penalty = 1.0 - (r->temp - 60.0) / 10.0;
    }

    /* Efficiency bonus: 1.0 + (GH/s per watt) * 0.1 */
    double efficiency_bonus = 1.0 + (double)r->efficiency_ghs_per_w * 0.1;

    return (double)r->hashrate_ghs * efficiency_bonus * temp_penalty;
}

const tuner_status_t *tuner_get_status(void)
{
    return &s_status;
}

void tuner_set_state(tuner_state_t state)
{
    s_status.state = state;
}

void tuner_reset_status(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = TUNER_STATE_IDLE;
    s_status.best_index = -1;
    s_status.best_eff_index = -1;
}

tuner_result_t *tuner_get_result_slot(int index)
{
    if (index < 0 || index >= TUNER_MAX_RESULTS) {
        return NULL;
    }
    return &s_status.results[index];
}

void tuner_set_result_count(int count)
{
    s_status.result_count = count;
}

void tuner_set_step(int current, int total)
{
    s_status.current_step = current;
    s_status.total_steps = total;
}

void tuner_set_best(int best_idx, int best_eff_idx)
{
    s_status.best_index = best_idx;
    s_status.best_eff_index = best_eff_idx;
}
